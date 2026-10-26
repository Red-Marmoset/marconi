// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One side of the file panel, like a file explorer: Back, Forward, Up and the clickable path; a folder tree;
// the folder's contents

#include "FilePane.h"

#include "Appearance.h"
#include "Controls.h"
#include "FilePanel.h"

#include <wx/artprov.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/button.h>
#include <wx/dataobj.h>
#include <wx/datetime.h>
#include <wx/dnd.h>
#include <wx/filename.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/treectrl.h>
#include <wx/utils.h>

#include <memory>

namespace MarconiUI
{
    namespace
    {
        // Files dragged between the two panes: "L" or "R" for the side they come from, then one path per line.
        const wxDataFormat& PaneFilesFormat()
        {
            static const wxDataFormat format("application/x-marconi-files");
            return format;
        }

        wxString FormatSize(const Marconi::Files::FileEntry& entry)
        {
            return entry.isFolder ? wxString() : wxFileName::GetHumanReadableSize(wxULongLong(entry.size));
        }

        wxString FormatModified(int64_t seconds)
        {
            return seconds > 0 ? wxDateTime(static_cast<time_t>(seconds)).Format("%Y-%m-%d %H:%M") : wxString();
        }

        // Remote files being dragged out of the panel. Nothing is downloaded until the drop target asks for the
        // files (when the user lets go), so a drag that ends back in the panel, or nowhere, costs nothing.
        class RemoteFilesData : public wxFileDataObject
        {
        public:
            RemoteFilesData(FilePanel& panel, std::vector<std::string> remotePaths)
                : m_panel(panel),
                  m_remotePaths(std::move(remotePaths))
            {
            }

            size_t GetDataSize() const override
            {
                Prepare();
                return wxFileDataObject::GetDataSize();
            }

            bool GetDataHere(void* buffer) const override
            {
                Prepare();
                return wxFileDataObject::GetDataHere(buffer);
            }

        private:
            void Prepare() const
            {
                if (m_prepared)
                {
                    return;
                }
                m_prepared = true;
                auto* self = const_cast<RemoteFilesData*>(this);
                for (const std::string& path : m_panel.DownloadForDrag(m_remotePaths))
                {
                    self->AddFile(wxString::FromUTF8(path));
                }
            }

            FilePanel& m_panel;
            std::vector<std::string> m_remotePaths;
            mutable bool m_prepared = false;
        };
    } // namespace

    struct FilePane::TreeData : public wxTreeItemData
    {
        explicit TreeData(std::string folder) : path(std::move(folder)) {}
        std::string path;
        bool loaded = false;
    };

    class FilePane::DropTarget : public wxDropTarget
    {
    public:
        explicit DropTarget(FilePane& pane) : m_pane(pane)
        {
            m_composite = new wxDataObjectComposite();
            m_files = new wxFileDataObject();
            m_paneFiles = new wxCustomDataObject(PaneFilesFormat());
            m_composite->Add(m_paneFiles, true);
            m_composite->Add(m_files);
            SetDataObject(m_composite);
        }

        wxDragResult OnData(wxCoord, wxCoord, wxDragResult result) override
        {
            if (!GetData())
            {
                return wxDragNone;
            }
            if (m_composite->GetReceivedFormat() == PaneFilesFormat())
            {
                wxString text = wxString::FromUTF8(static_cast<const char*>(m_paneFiles->GetData()),
                                                   m_paneFiles->GetSize());
                wxArrayString lines = wxSplit(text, '\n', '\0');
                if (lines.empty())
                {
                    return wxDragNone;
                }
                bool fromRemote = lines[0] == "R";
                std::vector<std::string> paths;
                for (size_t i = 1; i < lines.size(); ++i)
                {
                    if (!lines[i].empty())
                    {
                        paths.push_back(lines[i].utf8_string());
                    }
                }
                m_pane.OnDropped(paths, fromRemote);
                return result;
            }
            // From Explorer or Finder: these are files on this computer.
            std::vector<std::string> paths;
            for (const wxString& file : m_files->GetFilenames())
            {
                wxString path = file;
                path.Replace("\\", "/");
                paths.push_back(path.utf8_string());
            }
            m_pane.OnDropped(paths, false);
            return wxDragCopy;
        }

        wxDragResult OnDragOver(wxCoord, wxCoord, wxDragResult) override { return wxDragCopy; }

    private:
        FilePane& m_pane;
        wxDataObjectComposite* m_composite = nullptr;
        wxFileDataObject* m_files = nullptr;
        wxCustomDataObject* m_paneFiles = nullptr;
    };

    FilePane::FilePane(wxWindow* parent, FilePanel& owner, bool remote, const wxString& heading)
        : wxPanel(parent),
          m_owner(owner),
          m_remote(remote)
    {
        // Explicit minima stop a long path or host name from stealing the other pane's width.
        SetMinSize(FromDIP(wxSize(0, 0)));
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
        m_heading = new wxStaticText(this, wxID_ANY, heading, wxDefaultPosition, wxDefaultSize,
                                     wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
        m_heading->SetMinSize(FromDIP(wxSize(0, 24)));
        m_heading->SetFont(HeadingFont(GetFont(), 1.2f));
        m_heading->SetToolTip(heading);
        bar->Add(m_heading, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        auto barButton = [this, bar](const wxString& symbol, const wxString& tip) {
            auto* button = new ActionButton(this, wxID_ANY, symbol, false, wxSize(34, 34));
            button->SetFont(GetFont().Scaled(1.3f));
            button->SetToolTip(tip);
            button->SetName(tip);
            bar->Add(button, 0, wxLEFT, FromDIP(2));
            return button;
        };
        m_back = barButton(wxString::FromUTF8("\xE2\x80\xB9"), "Back");
        m_forward = barButton(wxString::FromUTF8("\xE2\x80\xBA"), "Forward");
        m_up = barButton(wxString::FromUTF8("\xE2\x86\x91"), "Up to the folder above (Backspace)");
        wxButton* refresh = barButton(wxString::FromUTF8("\xE2\x86\xBB"), "Refresh (F5)");
        sizer->Add(bar, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
        const int pathHeight = FromDIP(32) + wxSystemSettings::GetMetric(wxSYS_HSCROLL_Y, this);
        m_crumbs = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(1, pathHeight),
                                       wxHSCROLL | wxBORDER_NONE);
        m_crumbs->SetMinSize(wxSize(1, pathHeight));
        m_crumbs->SetScrollRate(FromDIP(8), 0);
        m_crumbs->SetSizer(new wxBoxSizer(wxHORIZONTAL));
        sizer->Add(m_crumbs, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        m_split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_NOBORDER);
        m_split->SetMinimumPaneSize(FromDIP(80));
        m_tree = new wxTreeCtrl(m_split, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_NONE);
        m_list = new ThemedListView(m_split, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_NONE);
        m_icons = new wxImageList(FromDIP(16), FromDIP(28), true);
        for (const wxArtID& art : {wxART_FOLDER, wxART_NORMAL_FILE, wxART_HARDDISK})
        {
            wxImage icon = wxArtProvider::GetBitmap(art, wxART_LIST, FromDIP(wxSize(16, 16))).ConvertToImage();
            m_icons->Add(wxBitmap(icon.Size(FromDIP(wxSize(16, 28)), FromDIP(wxPoint(0, 6)))));
        }
        m_tree->SetName(remote ? "Remote folders" : "Local folders");
        m_list->SetName(remote ? "Remote files" : "Local files");
        m_list->SetImageList(m_icons, wxIMAGE_LIST_SMALL);
        m_tree->SetImageList(m_icons);
        m_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(180));
        m_list->AppendColumn("Size", wxLIST_FORMAT_RIGHT, FromDIP(70));
        m_list->AppendColumn("Modified", wxLIST_FORMAT_LEFT, FromDIP(115));
        m_list->AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(80));
        m_split->SplitVertically(m_tree, m_list, FromDIP(112));
        m_split->SetSashGravity(0.0);
        m_tree->SetIndent(FromDIP(14));
        sizer->Add(m_split, 1, wxEXPAND);
        SetSizer(sizer);

        m_back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoBack(); });
        m_forward->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoForward(); });
        m_up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoUp(); });
        refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Reload(); });
        m_tree->Bind(wxEVT_TREE_ITEM_EXPANDING, [this](wxTreeEvent& event) { LoadTreeItem(event.GetItem()); });
        m_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& event) {
            if (!m_syncingTree && event.GetItem().IsOk())
            {
                if (auto* data = static_cast<TreeData*>(m_tree->GetItemData(event.GetItem())))
                {
                    Navigate(data->path);
                }
            }
        });
        m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { OnActivated(event); });
        m_list->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent&) { StartDrag(); });
        m_list->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& event) { SortBy(event.GetColumn()); });
        m_list->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent&) { ShowContextMenu(); });
        m_list->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) { OnKey(event); });
        m_list->SetDropTarget(new DropTarget(*this));
        UpdateButtons();
        Enable(false);
    }

    FilePane::~FilePane()
    {
        delete m_icons; // shared by the tree and the list, so neither owns it
    }

    void FilePane::SetSource(Marconi::Files::FileSource* source, const std::string& folder, const std::vector<Root>& roots)
    {
        m_work.Drain(); // nothing may still be using the old source
        ++m_generation;
        m_source = source;
        m_history.clear();
        m_future.clear();
        m_folder.clear();
        m_entries.clear();
        m_list->DeleteAllItems();
        m_tree->DeleteAllItems();
        m_treeRoot = m_tree->AddRoot("");
        Enable(source != nullptr);
        if (source == nullptr)
        {
            ShowCrumbs();
            UpdateButtons();
            return;
        }
        for (const Root& root : roots)
        {
            bool drive = root.path == "/" || (root.path.size() >= 2 && root.path[root.path.size() - 2] == ':');
            wxTreeItemId item = m_tree->AppendItem(m_treeRoot, root.label, drive ? 2 : 0, -1,
                                                   new TreeData(root.path));
            m_tree->SetItemHasChildren(item, true);
        }
        Navigate(folder);
    }

    void FilePane::SetHeading(const wxString& heading)
    {
        m_heading->SetLabel(heading);
        m_heading->SetToolTip(heading);
    }

    void FilePane::Navigate(const std::string& folder, bool remember)
    {
        if (m_source == nullptr || folder.empty())
        {
            return;
        }
        if (remember && !m_folder.empty() && folder != m_folder)
        {
            m_history.push_back(m_folder);
            m_future.clear();
        }
        int generation = ++m_generation;
        Marconi::Files::FileSource* source = m_source;
        m_work.Post([this, source, folder, generation]() {
            auto entries = std::make_shared<std::vector<Marconi::Files::FileEntry>>();
            std::string error;
            bool ok = source->List(folder, *entries, error);
            CallAfter([this, ok, folder, generation, entries, error]() {
                if (generation != m_generation)
                {
                    return; // the user has gone somewhere else since
                }
                if (!ok)
                {
                    if (!m_history.empty() && m_history.back() == m_folder)
                    {
                        m_history.pop_back();
                    }
                    wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
                    return;
                }
                ShowFolder(folder, std::move(*entries));
            });
        });
    }

    std::vector<std::string> FilePane::GetSelectedPaths() const
    {
        std::vector<std::string> paths;
        for (long row = m_list->GetFirstSelected(); row != -1; row = m_list->GetNextSelected(row))
        {
            if (row < static_cast<long>(m_entries.size()))
            {
                paths.push_back(Marconi::Files::JoinPath(m_folder, m_entries[static_cast<size_t>(row)].name));
            }
        }
        return paths;
    }

    // Paths dropped on this pane: from the other pane or from Explorer/Finder.
    void FilePane::OnDropped(const std::vector<std::string>& paths, bool fromRemote)
    {
        if (fromRemote == m_remote)
        {
            return; // dropped back where it came from
        }
        m_owner.CopyBetweenSides(fromRemote, paths);
    }

    void FilePane::ShowFolder(const std::string& folder, std::vector<Marconi::Files::FileEntry> entries)
    {
        m_folder = folder;
        m_entries = std::move(entries);
        Marconi::Files::SortEntries(m_entries, m_sortColumn, m_sortAscending);
        m_list->Freeze();
        m_list->DeleteAllItems();
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            const auto& entry = m_entries[i];
            long row = m_list->InsertItem(static_cast<long>(i), wxString::FromUTF8(entry.name),
                                          entry.isFolder ? 0 : 1);
            m_list->SetItem(row, 1, FormatSize(entry));
            m_list->SetItem(row, 2, FormatModified(entry.modified));
            m_list->SetItem(row, 3, wxString::FromUTF8(Marconi::Files::TypeName(entry)));
        }
        m_list->Thaw();
        ShowCrumbs();
        UpdateButtons();
        SelectInTree(folder);
    }

    // The path as buttons: clicking one goes there.
    void FilePane::ShowCrumbs()
    {
        m_crumbs->Freeze();
        m_crumbs->DestroyChildren();
        wxSizer* sizer = m_crumbs->GetSizer();
        sizer->Clear();
        std::vector<Marconi::Files::PathPart> parts = Marconi::Files::SplitPath(m_folder);
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0 && parts[i - 1].label != "/")
            {
                sizer->Add(new wxStaticText(m_crumbs, wxID_ANY, wxString::FromUTF8("\xE2\x80\xBA")), 0,
                           wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(2));
            }
            wxString label = parts[i].label == "/" && m_remote && m_source != nullptr ? wxString("/")
                                                                                        : wxString::FromUTF8(parts[i].label);
            wxButton* crumb = new ActionButton(m_crumbs, wxID_ANY, label, false, wxSize(-1, 30));
            crumb->SetToolTip(wxString::FromUTF8(parts[i].path));
            if (i + 1 == parts.size())
            {
                crumb->SetFont(crumb->GetFont().Bold());
            }
            std::string path = parts[i].path;
            crumb->Bind(wxEVT_BUTTON, [this, path](wxCommandEvent&) { Navigate(path); });
            sizer->Add(crumb, 0, wxALIGN_CENTER_VERTICAL);
        }
        StyleWindow(m_crumbs);
        m_crumbs->Layout();
        m_crumbs->FitInside();
        m_crumbs->Scroll(m_crumbs->GetVirtualSize().x / FromDIP(8), 0);
        m_crumbs->SetToolTip(wxString::FromUTF8(m_folder));
        m_crumbs->Thaw();
    }

    void FilePane::UpdateButtons()
    {
        m_back->Enable(!m_history.empty());
        m_forward->Enable(!m_future.empty());
        m_up->Enable(!m_folder.empty() && Marconi::Files::ParentPath(m_folder) != m_folder);
    }

    void FilePane::GoBack()
    {
        if (m_history.empty())
        {
            return;
        }
        m_future.push_back(m_folder);
        std::string folder = m_history.back();
        m_history.pop_back();
        Navigate(folder, false);
    }

    void FilePane::GoForward()
    {
        if (m_future.empty())
        {
            return;
        }
        m_history.push_back(m_folder);
        std::string folder = m_future.back();
        m_future.pop_back();
        Navigate(folder, false);
    }

    void FilePane::GoUp()
    {
        if (!m_folder.empty())
        {
            Navigate(Marconi::Files::ParentPath(m_folder));
        }
    }

    void FilePane::SortBy(int column)
    {
        static const Marconi::Files::SortColumn kColumns[] = {
            Marconi::Files::SortColumn::Name, Marconi::Files::SortColumn::Size,
            Marconi::Files::SortColumn::Modified, Marconi::Files::SortColumn::Type};
        if (column < 0 || column > 3)
        {
            return;
        }
        Marconi::Files::SortColumn chosen = kColumns[column];
        m_sortAscending = chosen == m_sortColumn ? !m_sortAscending : chosen != Marconi::Files::SortColumn::Modified;
        m_sortColumn = chosen;
        ShowFolder(m_folder, m_entries);
    }

    // Fills in a folder's subfolders the first time it's opened in the tree, in the background.
    void FilePane::LoadTreeItem(const wxTreeItemId& item)
    {
        auto* data = static_cast<TreeData*>(m_tree->GetItemData(item));
        if (data == nullptr || data->loaded || m_source == nullptr)
        {
            return;
        }
        data->loaded = true;
        m_tree->DeleteChildren(item);
        m_tree->AppendItem(item, wxString::FromUTF8("Loading\xE2\x80\xA6"));
        Marconi::Files::FileSource* source = m_source;
        std::string folder = data->path;
        int generation = m_generation;
        m_work.Post([this, source, folder, item, generation]() {
            auto entries = std::make_shared<std::vector<Marconi::Files::FileEntry>>();
            std::string error;
            bool ok = source->List(folder, *entries, error);
            CallAfter([this, ok, folder, item, generation, entries]() {
                if (!item.IsOk() || !m_source || FindTreeItem(m_treeRoot, folder) != item)
                {
                    return; // the tree was rebuilt meanwhile
                }
                (void)generation;
                m_tree->DeleteChildren(item);
                if (!ok)
                {
                    m_tree->SetItemHasChildren(item, false);
                    return;
                }
                Marconi::Files::SortEntries(*entries, Marconi::Files::SortColumn::Name, true);
                bool any = false;
                for (const auto& entry : *entries)
                {
                    if (!entry.isFolder)
                    {
                        continue;
                    }
                    wxTreeItemId child =
                        m_tree->AppendItem(item, wxString::FromUTF8(entry.name), 0, -1,
                                           new TreeData(Marconi::Files::JoinPath(folder, entry.name)));
                    m_tree->SetItemHasChildren(child, true);
                    any = true;
                }
                m_tree->SetItemHasChildren(item, any);
                SelectInTree(m_folder);
            });
        });
    }

    wxTreeItemId FilePane::FindTreeItem(const wxTreeItemId& parent, const std::string& path)
    {
        wxTreeItemIdValue cookie;
        for (wxTreeItemId child = m_tree->GetFirstChild(parent, cookie); child.IsOk();
             child = m_tree->GetNextChild(parent, cookie))
        {
            auto* data = static_cast<TreeData*>(m_tree->GetItemData(child));
            if (data != nullptr && data->path == path)
            {
                return child;
            }
            wxTreeItemId found = FindTreeItem(child, path);
            if (found.IsOk())
            {
                return found;
            }
        }
        return wxTreeItemId();
    }

    // Highlights the folder being shown, if the tree has it open.
    void FilePane::SelectInTree(const std::string& folder)
    {
        wxTreeItemId item = FindTreeItem(m_treeRoot, folder);
        if (item.IsOk() && m_tree->GetSelection() != item)
        {
            m_syncingTree = true;
            m_tree->SelectItem(item);
            m_tree->EnsureVisible(item);
            m_syncingTree = false;
        }
    }

    void FilePane::OnActivated(wxListEvent& event)
    {
        long row = event.GetIndex();
        if (row >= 0 && row < static_cast<long>(m_entries.size()) && m_entries[static_cast<size_t>(row)].isFolder)
        {
            Navigate(Marconi::Files::JoinPath(m_folder, m_entries[static_cast<size_t>(row)].name));
        }
    }

    void FilePane::OnKey(wxKeyEvent& event)
    {
        if (event.GetKeyCode() == WXK_BACK)
        {
            GoUp();
        }
        else if (event.GetKeyCode() == WXK_DELETE)
        {
            DeleteSelected();
        }
        else if (event.GetKeyCode() == WXK_F5)
        {
            Reload();
        }
        else if (event.GetKeyCode() == 'A' && event.ControlDown())
        {
            for (long row = 0; row < m_list->GetItemCount(); ++row)
            {
                m_list->Select(row);
            }
        }
        else if (event.GetKeyCode() == 'C' && event.ControlDown())
        {
            m_owner.CopyToClipboard(m_remote, GetSelectedPaths());
        }
        else if (event.GetKeyCode() == 'V' && event.ControlDown() && m_remote)
        {
            m_owner.PasteFromClipboard();
        }
        else
        {
            event.Skip();
        }
    }

    void FilePane::StartDrag()
    {
        std::vector<std::string> paths = GetSelectedPaths();
        if (paths.empty())
        {
            return;
        }
        std::string text = m_remote ? "R" : "L";
        for (const std::string& path : paths)
        {
            text += "\n" + path;
        }
        wxDataObjectComposite* data = new wxDataObjectComposite();
        wxCustomDataObject* paneFiles = new wxCustomDataObject(PaneFilesFormat());
        paneFiles->SetData(text.size(), text.data());
        data->Add(paneFiles, true);
        if (!m_remote)
        {
            // This computer's files can also be dropped on Explorer or Finder, which copies them.
            wxFileDataObject* files = new wxFileDataObject();
            for (const std::string& path : paths)
            {
                files->AddFile(wxString::FromUTF8(path));
            }
            data->Add(files);
        }
        else
        {
            // Remote files dropped on Explorer or Finder are downloaded when the user lets go.
            data->Add(new RemoteFilesData(m_owner, paths));
        }
        wxDropSource source(*data, this);
        source.DoDragDrop(wxDrag_CopyOnly);
        delete data;
    }

    void FilePane::ShowContextMenu()
    {
        enum
        {
            ID_NEW_FOLDER = wxID_HIGHEST + 300,
            ID_RENAME,
            ID_DELETE,
            ID_COPY_ACROSS,
        };
        wxMenu menu;
        menu.Append(ID_COPY_ACROSS, m_remote ? "&Download" : "&Upload");
        menu.AppendSeparator();
        menu.Append(ID_NEW_FOLDER, "&New Folder");
        menu.Append(ID_RENAME, "&Rename");
        menu.Append(ID_DELETE, "&Delete");
        size_t selected = GetSelectedPaths().size();
        menu.Enable(ID_COPY_ACROSS, selected > 0);
        menu.Enable(ID_RENAME, selected == 1);
        menu.Enable(ID_DELETE, selected > 0);
        switch (GetPopupMenuSelectionFromUser(menu))
        {
            case ID_COPY_ACROSS: m_owner.CopyBetweenSides(m_remote, GetSelectedPaths()); break;
            case ID_NEW_FOLDER: MakeFolder(); break;
            case ID_RENAME: RenameSelected(); break;
            case ID_DELETE: DeleteSelected(); break;
            default: break;
        }
    }

    void FilePane::MakeFolder()
    {
        wxString name = wxGetTextFromUser("Name of the new folder:", "New Folder", "New Folder", this);
        if (name.empty())
        {
            return;
        }
        std::string path = Marconi::Files::JoinPath(m_folder, name.utf8_string());
        Change([path](Marconi::Files::FileSource& source, std::string& error) {
            return source.MakeFolder(path, error);
        });
    }

    void FilePane::RenameSelected()
    {
        std::vector<std::string> paths = GetSelectedPaths();
        if (paths.size() != 1)
        {
            return;
        }
        wxString oldName = wxString::FromUTF8(Marconi::Files::FileName(paths[0]));
        wxString name = wxGetTextFromUser("New name:", "Rename", oldName, this);
        if (name.empty() || name == oldName)
        {
            return;
        }
        std::string from = paths[0];
        std::string to = Marconi::Files::JoinPath(m_folder, name.utf8_string());
        Change([from, to](Marconi::Files::FileSource& source, std::string& error) {
            return source.Rename(from, to, error);
        });
    }

    void FilePane::DeleteSelected()
    {
        std::vector<std::string> paths = GetSelectedPaths();
        if (paths.empty())
        {
            return;
        }
        wxString what = paths.size() == 1 ? "\"" + wxString::FromUTF8(Marconi::Files::FileName(paths[0])) + "\""
                                          : wxString::Format("these %zu items", paths.size());
        if (wxMessageBox("Delete " + what + "? This can't be undone.", "Delete", wxYES_NO | wxICON_WARNING,
                         this) != wxYES)
        {
            return;
        }
        Change([paths](Marconi::Files::FileSource& source, std::string& error) {
            for (const std::string& path : paths)
            {
                if (!DeleteTree(source, path, error))
                {
                    return false;
                }
            }
            return true;
        });
    }

    void FilePane::Change(std::function<bool(Marconi::Files::FileSource&, std::string&)> change)
    {
        if (m_source == nullptr)
        {
            return;
        }
        // On the worker thread, like a listing: the source may be the other computer, over a slow link.
        Marconi::Files::FileSource* source = m_source;
        m_work.Post([this, source, change]() {
            std::string error;
            bool ok = change(*source, error);
            CallAfter([this, source, ok, error]() {
                if (source != m_source)
                {
                    return; // the pane shows something else now
                }
                if (!ok)
                {
                    wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
                }
                Reload();
            });
        });
    }

    bool FilePane::DeleteTree(Marconi::Files::FileSource& source, const std::string& path, std::string& error)
    {
        Marconi::Files::FileEntry entry;
        if (!source.Stat(path, entry, error))
        {
            return false;
        }
        if (!entry.isFolder)
        {
            return source.Remove(path, error);
        }
        std::vector<Marconi::Files::FileEntry> children;
        if (!source.List(path, children, error))
        {
            return false;
        }
        for (const auto& child : children)
        {
            if (!DeleteTree(source, Marconi::Files::JoinPath(path, child.name), error))
            {
                return false;
            }
        }
        return source.RemoveFolder(path, error);
    }
} // namespace MarconiUI
