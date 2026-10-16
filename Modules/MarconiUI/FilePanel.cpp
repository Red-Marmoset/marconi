// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The file transfer panel: this computer's files beside the remote computer's, with copying both ways

#include "FilePanel.h"

#include "Appearance.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"

#include <Log/Log.h>

#include <wx/artprov.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/imaglist.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/volume.h>
#include <wx/datetime.h>
#include <wx/dnd.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <MarconiCore/Files/FileView.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <random>

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

        wxString FormatState(const Marconi::Files::TransferStatus& status)
        {
            using Marconi::Files::TransferState;
            switch (status.state)
            {
                case TransferState::Waiting: return "Waiting";
                case TransferState::Copying:
                    return status.total > 0 ? wxString::Format("%d%%", static_cast<int>(status.done * 100 / status.total))
                                            : wxString("Copying");
                case TransferState::Done: return "Done";
                case TransferState::Skipped: return "Skipped";
                case TransferState::Failed: return "Failed: " + wxString::FromUTF8(status.error);
                case TransferState::Cancelled: return "Cancelled";
            }
            return wxString();
        }
    } // namespace

    namespace
    {
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

    // Runs file operations one at a time on a worker thread, so a slow folder listing never freezes the window.
    class BackgroundWork
    {
    public:
        BackgroundWork() : m_thread([this]() { Run(); }) {}
        ~BackgroundWork() { Stop(); }

        void Post(std::function<void()> job)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    return;
                }
                m_jobs.push_back(std::move(job));
            }
            m_changed.notify_all();
        }

        // Drops what's waiting and waits for the job in progress, e.g. before the file source changes.
        void Drain()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_jobs.clear();
            m_changed.wait(lock, [this]() { return !m_busy; });
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    return;
                }
                m_stopping = true;
                m_jobs.clear();
            }
            m_changed.notify_all();
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

    private:
        void Run()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_changed.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
                    if (m_stopping)
                    {
                        return;
                    }
                    job = std::move(m_jobs.front());
                    m_jobs.pop_front();
                    m_busy = true;
                }
                job();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_busy = false;
                }
                m_changed.notify_all();
            }
        }

        std::mutex m_mutex;
        std::condition_variable m_changed;
        std::deque<std::function<void()>> m_jobs;
        bool m_busy = false;
        bool m_stopping = false;
        std::thread m_thread;
    };

    // One side, like a file explorer: Back, Forward, Up and the clickable path; a folder tree; the folder's contents.
    class FilePane : public wxPanel
    {
    public:
        struct Root
        {
            wxString label;
            std::string path;
        };

        FilePane(wxWindow* parent, FilePanel& owner, bool remote, const wxString& heading)
            : wxPanel(parent),
              m_owner(owner),
              m_remote(remote)
        {
            wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
            m_heading = new wxStaticText(this, wxID_ANY, heading);
            m_heading->SetFont(m_heading->GetFont().Bold());
            sizer->Add(m_heading, 0, wxBOTTOM, FromDIP(4));

            wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
            auto barButton = [this, bar](const wxArtID& art, const wxString& tip) {
                wxBitmapButton* button = new wxBitmapButton(
                    this, wxID_ANY, wxArtProvider::GetBitmapBundle(art, wxART_BUTTON, FromDIP(wxSize(16, 16))));
                button->SetToolTip(tip);
                bar->Add(button, 0, wxRIGHT, FromDIP(2));
                return button;
            };
            m_back = barButton(wxART_GO_BACK, "Back");
            m_forward = barButton(wxART_GO_FORWARD, "Forward");
            m_up = barButton(wxART_GO_UP, "Up to the folder above (Backspace)");
            wxBitmapButton* refresh = barButton(wxART_REDO, "Refresh (F5)");
            m_crumbs = new wxPanel(this);
            m_crumbs->SetSizer(new wxBoxSizer(wxHORIZONTAL));
            bar->Add(m_crumbs, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
            sizer->Add(bar, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

            m_split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE | wxSP_3DSASH);
            m_split->SetMinimumPaneSize(FromDIP(80));
            m_tree = new wxTreeCtrl(m_split, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SIMPLE);
            m_list = new wxListView(m_split, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_SIMPLE);
            m_icons = new wxImageList(FromDIP(16), FromDIP(16), true);
            m_icons->Add(wxArtProvider::GetIcon(wxART_FOLDER, wxART_LIST, FromDIP(wxSize(16, 16))));
            m_icons->Add(wxArtProvider::GetIcon(wxART_NORMAL_FILE, wxART_LIST, FromDIP(wxSize(16, 16))));
            m_icons->Add(wxArtProvider::GetIcon(wxART_HARDDISK, wxART_LIST, FromDIP(wxSize(16, 16))));
            m_list->SetImageList(m_icons, wxIMAGE_LIST_SMALL);
            m_tree->SetImageList(m_icons);
            m_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(180));
            m_list->AppendColumn("Size", wxLIST_FORMAT_RIGHT, FromDIP(70));
            m_list->AppendColumn("Modified", wxLIST_FORMAT_LEFT, FromDIP(115));
            m_list->AppendColumn("Type", wxLIST_FORMAT_LEFT, FromDIP(80));
            m_split->SplitVertically(m_tree, m_list, FromDIP(160));
            m_split->SetSashGravity(0.0);
            sizer->Add(m_split, 1, wxEXPAND);
            SetSizer(sizer);

            m_back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoBack(); });
            m_forward->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoForward(); });
            m_up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoUp(); });
            refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Refresh(); });
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

        ~FilePane() override
        {
            delete m_icons; // shared by the tree and the list, so neither owns it
        }

        bool IsRemote() const { return m_remote; }
        const std::string& GetFolder() const { return m_folder; }
        Marconi::Files::FileSource* GetSource() const { return m_source; }

        // Stops background listings; call before the file source goes away.
        void StopWork() { m_work.Stop(); }

        void SetSource(Marconi::Files::FileSource* source, const std::string& folder, const std::vector<Root>& roots)
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

        void SetHeading(const wxString& heading) { m_heading->SetLabel(heading); }

        void Navigate(const std::string& folder, bool remember = true)
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

        void Refresh() { Navigate(m_folder, false); }

        std::vector<std::string> GetSelectedPaths() const
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
        void OnDropped(const std::vector<std::string>& paths, bool fromRemote)
        {
            if (fromRemote == m_remote)
            {
                return; // dropped back where it came from
            }
            m_owner.CopyBetweenSides(fromRemote, paths);
        }

    private:
        struct TreeData : public wxTreeItemData
        {
            explicit TreeData(std::string folder) : path(std::move(folder)) {}
            std::string path;
            bool loaded = false;
        };

        class DropTarget : public wxDropTarget
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

        void ShowFolder(const std::string& folder, std::vector<Marconi::Files::FileEntry> entries)
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
        void ShowCrumbs()
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
                wxButton* crumb = new wxButton(m_crumbs, wxID_ANY, label, wxDefaultPosition, wxDefaultSize,
                                               wxBU_EXACTFIT | wxBORDER_NONE);
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
            m_crumbs->Thaw();
        }

        void UpdateButtons()
        {
            m_back->Enable(!m_history.empty());
            m_forward->Enable(!m_future.empty());
            m_up->Enable(!m_folder.empty() && Marconi::Files::ParentPath(m_folder) != m_folder);
        }

        void GoBack()
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

        void GoForward()
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

        void GoUp()
        {
            if (!m_folder.empty())
            {
                Navigate(Marconi::Files::ParentPath(m_folder));
            }
        }

        void SortBy(int column)
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
        void LoadTreeItem(const wxTreeItemId& item)
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

        wxTreeItemId FindTreeItem(const wxTreeItemId& parent, const std::string& path)
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
        void SelectInTree(const std::string& folder)
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

        void OnActivated(wxListEvent& event)
        {
            long row = event.GetIndex();
            if (row >= 0 && row < static_cast<long>(m_entries.size()) && m_entries[static_cast<size_t>(row)].isFolder)
            {
                Navigate(Marconi::Files::JoinPath(m_folder, m_entries[static_cast<size_t>(row)].name));
            }
        }

        void OnKey(wxKeyEvent& event)
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
                Refresh();
            }
            else if (event.GetKeyCode() == 'A' && event.ControlDown())
            {
                for (long row = 0; row < m_list->GetItemCount(); ++row)
                {
                    m_list->Select(row);
                }
            }
            else
            {
                event.Skip();
            }
        }

        void StartDrag()
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

        void ShowContextMenu()
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

        void MakeFolder()
        {
            wxString name = wxGetTextFromUser("Name of the new folder:", "New Folder", "New Folder", this);
            if (name.empty())
            {
                return;
            }
            std::string error;
            if (!m_source->MakeFolder(Marconi::Files::JoinPath(m_folder, name.utf8_string()), error))
            {
                wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
            }
            Refresh();
        }

        void RenameSelected()
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
            std::string error;
            if (!m_source->Rename(paths[0], Marconi::Files::JoinPath(m_folder, name.utf8_string()), error))
            {
                wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
            }
            Refresh();
        }

        void DeleteSelected()
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
            for (const std::string& path : paths)
            {
                std::string error;
                if (!DeleteTree(path, error))
                {
                    wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
                    break;
                }
            }
            Refresh();
        }

        bool DeleteTree(const std::string& path, std::string& error)
        {
            Marconi::Files::FileEntry entry;
            if (!m_source->Stat(path, entry, error))
            {
                return false;
            }
            if (!entry.isFolder)
            {
                return m_source->Remove(path, error);
            }
            std::vector<Marconi::Files::FileEntry> children;
            if (!m_source->List(path, children, error))
            {
                return false;
            }
            for (const auto& child : children)
            {
                if (!DeleteTree(Marconi::Files::JoinPath(path, child.name), error))
                {
                    return false;
                }
            }
            return m_source->RemoveFolder(path, error);
        }

        FilePanel& m_owner;
        bool m_remote = false;
        Marconi::Files::FileSource* m_source = nullptr;
        std::string m_folder;
        std::vector<Marconi::Files::FileEntry> m_entries;
        std::vector<std::string> m_history; // for Back
        std::vector<std::string> m_future;  // for Forward
        int m_generation = 0;               // the latest listing asked for; older answers are dropped
        bool m_syncingTree = false;
        Marconi::Files::SortColumn m_sortColumn = Marconi::Files::SortColumn::Name;
        bool m_sortAscending = true;
        wxStaticText* m_heading = nullptr;
        wxBitmapButton* m_back = nullptr;
        wxBitmapButton* m_forward = nullptr;
        wxBitmapButton* m_up = nullptr;
        wxPanel* m_crumbs = nullptr;
        wxSplitterWindow* m_split = nullptr;
        wxTreeCtrl* m_tree = nullptr;
        wxListView* m_list = nullptr;
        wxImageList* m_icons = nullptr;
        wxTreeItemId m_treeRoot;
        BackgroundWork m_work; // last, so it stops first
    };

    namespace
    {
        // This computer: the home folder, then the drives (Windows) or the root.
        std::vector<FilePane::Root> LocalRoots(const std::string& home)
        {
            std::vector<FilePane::Root> roots = {{"Home", home}};
#if defined(__WXMSW__)
            for (const wxString& volume : wxFSVolume::GetVolumes(wxFS_VOL_MOUNTED))
            {
                wxString path = volume;
                path.Replace("\\", "/");
                wxString label = path;
                label.RemoveLast();
                roots.push_back({label, path.utf8_string()});
            }
#else
            roots.push_back({"Computer", "/"});
#endif
            return roots;
        }
    } // namespace

    FilePanel::FilePanel(wxWindow* parent, const Remote& remote) : wxPanel(parent), m_remote(remote)
    {
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        m_message = new wxStaticText(this, wxID_ANY, "");
        sizer->Add(m_message, 0, wxEXPAND | wxALL, FromDIP(6));

        wxBoxSizer* panes = new wxBoxSizer(wxHORIZONTAL);
        m_localPane = new FilePane(this, *this, false, "This computer");
        m_remotePane = new FilePane(this, *this, true, m_remote.displayName);
        wxBoxSizer* middle = new wxBoxSizer(wxVERTICAL);
        m_upload = new wxButton(this, wxID_ANY, wxString::FromUTF8("Upload \xE2\x86\x92"));
        m_download = new wxButton(this, wxID_ANY, wxString::FromUTF8("\xE2\x86\x90 Download"));
        m_upload->SetToolTip("Copy the files selected on this computer to the remote computer's folder");
        m_download->SetToolTip("Copy the files selected on the remote computer to this computer's folder");
        middle->AddStretchSpacer();
        middle->Add(m_upload, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
        middle->Add(m_download, 0, wxEXPAND);
        middle->AddStretchSpacer();
        panes->Add(m_localPane, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        panes->Add(middle, 0, wxEXPAND | wxRIGHT, FromDIP(6));
        panes->Add(m_remotePane, 1, wxEXPAND);
        sizer->Add(panes, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

        wxBoxSizer* transfersBar = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* transfersHeading = new wxStaticText(this, wxID_ANY, "Transfers");
        transfersHeading->SetFont(transfersHeading->GetFont().Bold());
        m_cancel = new wxButton(this, wxID_ANY, "Cancel All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        transfersBar->Add(transfersHeading, 1, wxALIGN_CENTER_VERTICAL);
        transfersBar->Add(m_cancel, 0);
        sizer->Add(transfersBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
        m_transfers = new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 60)),
                                     wxLC_REPORT | wxLC_NO_HEADER | wxBORDER_SIMPLE);
        m_transfers->SetMinSize(FromDIP(wxSize(-1, 72)));
        m_transfers->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(260));
        m_transfers->AppendColumn("Progress", wxLIST_FORMAT_LEFT, FromDIP(260));
        sizer->Add(m_transfers, 0, wxEXPAND | wxALL, FromDIP(6));
        SetSizer(sizer);

        m_upload->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            CopyBetweenSides(false, m_localPane->GetSelectedPaths());
        });
        m_download->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            CopyBetweenSides(true, m_remotePane->GetSelectedPaths());
        });
        m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_queue)
            {
                m_queue->Cancel();
            }
        });

        m_queue = std::make_unique<Marconi::Files::TransferQueue>(
            [this](const Marconi::Files::TransferStatus& status) {
                if (!m_closing)
                {
                    CallAfter([this, status]() { ShowStatus(status); });
                }
            },
            [this](const std::string& path) { return AskAboutExisting(path); });

        m_localPane->SetSource(&m_local, m_local.GetHome(), LocalRoots(m_local.GetHome()));
        m_upload->Enable(false);
        m_download->Enable(false);
        SetMessage("Not connected yet.");
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            if (!m_messageText.empty())
            {
                CallAfter([this]() { SetMessage(m_messageText); }); // wrap to the new width
            }
        });
        StyleWindow(this);
    }

    FilePanel::~FilePanel()
    {
        Shutdown();
    }

    std::vector<std::string> FilePanel::DownloadForDrag(const std::vector<std::string>& remotePaths)
    {
        std::vector<std::string> copies;
        Marconi::Files::FileSource* remote = GetRemote();
        if (remote == nullptr || remotePaths.empty())
        {
            return copies;
        }
        if (m_tight != nullptr && m_queue && m_queue->IsBusy())
        {
            // TightVNC can only send one file at a time on a connection.
            SetMessage("Wait for the current copy to finish before dragging files out.");
            return copies;
        }
        std::random_device random;
        wxString folder = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + "Marconi drag " +
                          wxString::Format("%08x", static_cast<unsigned>(random()));
        if (!wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
        {
            return copies;
        }
        folder.Replace("\\", "/");
        std::string destination = folder.utf8_string();
        m_dragFolders.push_back(destination);

        // Copy on a worker as usual, but wait here: the drop can't finish until the files exist.
        wxBusyCursor busy;
        SetMessage(wxString::Format("Downloading %zu item%s", remotePaths.size(), remotePaths.size() == 1 ? "" : "s") +
                   wxString::FromUTF8("\xE2\x80\xA6"));
        {
            Marconi::Files::TransferQueue queue(
                [this](const Marconi::Files::TransferStatus& status) {
                    if (!m_closing)
                    {
                        CallAfter([this, status]() { ShowStatus(status); });
                    }
                },
                [](const std::string&) { return Marconi::Files::ConflictAnswer{Marconi::Files::Conflict::Replace, true}; });
            queue.Add(*remote, remotePaths, m_local, destination);
            queue.WaitUntilIdle();
        }
        SetMessage("");
        for (const std::string& path : remotePaths)
        {
            std::string copy = Marconi::Files::JoinPath(destination, Marconi::Files::FileName(path));
            Marconi::Files::FileEntry entry;
            std::string error;
            if (m_local.Stat(copy, entry, error))
            {
                copies.push_back(copy);
            }
        }
        return copies;
    }

    void FilePanel::Shutdown()
    {
        m_closing = true;
        if (m_sftp)
        {
            m_sftp->Abort();
        }
        m_localPane->StopWork(); // their listings use the file sources that go next
        m_remotePane->StopWork();
        m_queue.reset(); // cancels and waits for the copy in progress
        JoinWorker();
        // The temporary copies made for drags out: Explorer or Finder has copied them by now.
        for (const std::string& folder : m_dragFolders)
        {
            wxFileName::Rmdir(wxString::FromUTF8(folder), wxPATH_RMDIR_RECURSIVE);
        }
        m_dragFolders.clear();
    }

    void FilePanel::JoinWorker()
    {
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    bool FilePanel::IsBusy() const
    {
        return m_queue && m_queue->IsBusy();
    }

    void FilePanel::SetMessage(const wxString& message)
    {
        m_messageText = message;
        m_message->SetLabel(message);
        m_message->Wrap(std::max(GetClientSize().x - FromDIP(12), FromDIP(200)));
        m_message->Show(!message.empty());
        Layout();
    }

    void FilePanel::Activate()
    {
        if (m_tight != nullptr)
        {
            if (m_state != State::Connected)
            {
                ShowRemote(m_tight, m_tight->GetHome());
            }
            return;
        }
        if (m_state == State::Idle || m_state == State::Failed)
        {
            Connect();
        }
    }

    void FilePanel::SetTightFiles(Marconi::Files::TightFiles* files, const wxString& displayName)
    {
        if (files == m_tight && (files == nullptr || m_state == State::Connected))
        {
            return;
        }
        m_tight = files;
        m_remote.displayName = displayName;
        m_remotePane->SetHeading(displayName);
        if (files == nullptr)
        {
            if (!m_sftp || !m_sftp->IsLoggedIn())
            {
                m_state = State::Idle;
                m_remotePane->SetSource(nullptr, "", {});
                m_upload->Enable(false);
                m_download->Enable(false);
            }
            return;
        }
        m_state = State::Idle;
        if (IsShownOnScreen())
        {
            Activate();
        }
    }

    Marconi::Files::FileSource* FilePanel::GetRemote()
    {
        if (m_tight != nullptr)
        {
            return m_tight;
        }
        return m_sftp && m_sftp->IsLoggedIn() ? m_sftp.get() : nullptr;
    }

    void FilePanel::ShowRemote(Marconi::Files::FileSource* source, const std::string& folder)
    {
        m_state = State::Connected;
        SetMessage("");
        m_remotePane->SetHeading(m_remote.displayName);
        std::vector<FilePane::Root> roots;
        if (source == m_tight)
        {
            roots.push_back({"Drives", "/"}); // TightVNC lists the drives at "/"
        }
        else
        {
            roots.push_back({"Home", source->GetHome()});
            roots.push_back({"Computer", "/"});
        }
        m_remotePane->SetSource(source, folder, roots);
        m_upload->Enable(true);
        m_download->Enable(true);
        if (!m_pendingDesktopUploads.empty())
        {
            std::vector<std::string> pending;
            pending.swap(m_pendingDesktopUploads);
            UploadToDesktop(pending);
        }
    }

    void FilePanel::Connect()
    {
        if (m_remote.host.empty())
        {
            SetMessage("There's no computer to connect to.");
            return;
        }
        if (m_queue && m_queue->IsBusy())
        {
            return; // the copy in progress still uses the current connection
        }
        m_state = State::Connecting;
        SetMessage("Connecting to " + m_remote.displayName + " for file transfer" + wxString::FromUTF8("\xE2\x80\xA6"));
        JoinWorker();
        m_remotePane->SetSource(nullptr, "", {});
        m_sftp = std::make_unique<Marconi::Files::SftpFiles>();
        Marconi::Files::SftpFiles* sftp = m_sftp.get();
        std::string host = m_remote.host;
        uint16_t port = m_remote.port;
        m_worker = std::thread([this, sftp, host, port]() {
            std::string error;
            bool ok = sftp->Connect(host, port, 10000, error, &m_closing);
            if (!m_closing)
            {
                CallAfter([this, ok, error]() { HandleConnected(ok, error); });
            }
        });
    }

    void FilePanel::HandleConnected(bool ok, const std::string& error)
    {
        if (!ok)
        {
            m_state = State::Failed;
            LOG_INFO("Files", "SFTP connection to %s failed: %s", m_remote.host.c_str(), error.c_str());
            SetMessage("Couldn't connect for file transfer: " + wxString::FromUTF8(error) +
                       "\nOn a Mac, turn on System Settings > General > Sharing > Remote Login.");
            return;
        }
        const Crypto::SshHostKey& key = m_sftp->GetHostKey();
        wxString fingerprint = wxString::FromUTF8(key.fingerprint);
        bool newKeyAccepted = false;
        if (!m_remote.trustedKey.empty() && m_remote.trustedKey != fingerprint)
        {
            LOG_WARNING("Files", "SSH host key for %s changed: was %s, now %s", m_remote.host.c_str(),
                        m_remote.trustedKey.utf8_string().c_str(), key.fingerprint.c_str());
            wxMessageDialog changed(this,
                                    m_remote.displayName + " identified itself differently from last time.\n\nThis "
                                    "happens when the computer has been reinstalled, but it can also mean another "
                                    "computer is pretending to be it. Only trust it if you know why it changed.\n\nWas: " +
                                        m_remote.trustedKey + "\nNow: " + fingerprint,
                                    "This computer has changed", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
            changed.SetYesNoLabels("Trust the New One", "Cancel");
            if (changed.ShowModal() != wxID_YES)
            {
                m_state = State::Failed;
                m_sftp->Abort();
                SetMessage("Refused: " + m_remote.displayName + " didn't identify itself the way it did before.");
                return;
            }
            newKeyAccepted = true;
        }
        if (m_remote.trustedKey.empty() && !newKeyAccepted)
        {
            wxMessageDialog trust(this,
                                  "Is this " + m_remote.displayName + "?\n\nThis is the first time Marconi transfers "
                                  "files with it. Its security fingerprint is:\n\n" + fingerprint +
                                      "\n\nMarconi will remember it, and warn you if it ever changes.",
                                  "Trust this computer?", wxYES_NO | wxICON_QUESTION);
            trust.SetYesNoLabels("Trust", "Cancel");
            if (trust.ShowModal() != wxID_YES)
            {
                m_state = State::Failed;
                m_sftp->Abort();
                SetMessage("Not connected: the computer wasn't trusted.");
                return;
            }
        }
        if (m_remote.trustedKey != fingerprint)
        {
            m_remote.trustedKey = fingerprint;
            if (!m_remote.savedId.empty())
            {
                ConnectionStore store;
                SavedConnection saved;
                if (store.Load(m_remote.savedId, saved))
                {
                    saved.sshHostKey = fingerprint;
                    store.Save(saved);
                }
            }
        }
        if (m_remote.username.empty() || m_remote.password.empty())
        {
            CredentialsDialog dialog(this, m_remote.displayName, Marconi::Rfb::CredentialsKind::Account,
                                     wxString::FromUTF8(m_remote.username), false);
            if (dialog.ShowModal() != wxID_OK)
            {
                m_state = State::Failed;
                m_sftp->Abort();
                SetMessage("Not connected.");
                return;
            }
            m_remote.username = dialog.GetUsername().utf8_string();
            m_remote.password = dialog.GetPassword().utf8_string();
        }
        Login();
    }

    void FilePanel::Login()
    {
        SetMessage("Logging in to " + m_remote.displayName + wxString::FromUTF8("\xE2\x80\xA6"));
        JoinWorker();
        Marconi::Files::SftpFiles* sftp = m_sftp.get();
        std::string user = m_remote.username;
        std::string password = m_remote.password;
        m_worker = std::thread([this, sftp, user, password]() {
            std::string error;
            bool ok = sftp->Login(user, password, error);
            if (!m_closing)
            {
                CallAfter([this, ok, error]() { HandleLoggedIn(ok, error); });
            }
        });
    }

    void FilePanel::HandleLoggedIn(bool ok, const std::string& error)
    {
        if (!ok)
        {
            m_state = State::Failed;
            m_remote.password.clear(); // ask again next time
            SetMessage("Couldn't log in for file transfer: " + wxString::FromUTF8(error));
            return;
        }
        ShowRemote(m_sftp.get(), m_sftp->GetHome());
    }

    void FilePanel::CopyBetweenSides(bool fromRemote, const std::vector<std::string>& paths)
    {
        Marconi::Files::FileSource* remote = GetRemote();
        if (paths.empty() || m_state != State::Connected || remote == nullptr)
        {
            return;
        }
        FilePane* destination = fromRemote ? m_localPane : m_remotePane;
        Marconi::Files::FileSource& local = m_local;
        StartCopy(fromRemote ? *remote : local, paths, fromRemote ? local : *remote, destination->GetFolder(),
                  destination);
    }

    void FilePanel::UploadToDesktop(const std::vector<std::string>& localPaths)
    {
        if (localPaths.empty())
        {
            return;
        }
        if (m_state != State::Connected)
        {
            m_pendingDesktopUploads.insert(m_pendingDesktopUploads.end(), localPaths.begin(), localPaths.end());
            Activate();
            return;
        }
        // A fixed folder: neither VNC nor SSH says which window or folder is under the pointer.
        Marconi::Files::FileSource* remote = GetRemote();
        if (remote == nullptr)
        {
            return;
        }
        std::string desktop;
        if (m_tight != nullptr)
        {
            // TightVNC doesn't say which Windows user is logged in: ask once, and remember.
            if (m_remote.dropFolder.empty())
            {
                wxString folder = wxGetTextFromUser(
                    "Files dropped on " + m_remote.displayName + "'s screen are copied to this folder on it. "
                    "The Public Desktop shows on everyone's desktop.",
                    "Where should dropped files go?", "/C:/Users/Public/Desktop", this);
                if (folder.empty())
                {
                    return;
                }
                folder.Replace("\\", "/");
                m_remote.dropFolder = folder;
                if (!m_remote.savedId.empty())
                {
                    ConnectionStore store;
                    SavedConnection saved;
                    if (store.Load(m_remote.savedId, saved))
                    {
                        saved.dropFolder = folder;
                        store.Save(saved);
                    }
                }
            }
            desktop = m_remote.dropFolder.utf8_string();
        }
        else
        {
            desktop = Marconi::Files::JoinPath(m_sftp->GetHome(), "Desktop");
            Marconi::Files::FileEntry entry;
            std::string error;
            if (!m_sftp->Stat(desktop, entry, error) || !entry.isFolder)
            {
                desktop = m_sftp->GetHome();
            }
        }
        StartCopy(m_local, localPaths, *remote, desktop, m_remotePane);
        SetMessage(wxString::Format("Copying %zu item%s to ", localPaths.size(), localPaths.size() == 1 ? "" : "s") +
                   wxString::FromUTF8(desktop));
    }

    void FilePanel::StartCopy(Marconi::Files::FileSource& from, const std::vector<std::string>& paths,
                              Marconi::Files::FileSource& to, const std::string& destination, FilePane* refreshAfter)
    {
        if (!m_queue)
        {
            return; // shut down
        }
        std::vector<int> ids = m_queue->Add(from, paths, to, destination);
        if (refreshAfter != nullptr && !ids.empty())
        {
            m_refreshWhenDone[ids.back()] = refreshAfter;
        }
    }

    void FilePanel::ShowStatus(const Marconi::Files::TransferStatus& status)
    {
        auto row = m_transferRows.find(status.id);
        long index = 0;
        if (row == m_transferRows.end())
        {
            index = m_transfers->InsertItem(m_transfers->GetItemCount(), wxString::FromUTF8(status.name));
            m_transferRows[status.id] = index;
        }
        else
        {
            index = row->second;
        }
        m_transfers->SetItem(index, 1, FormatState(status));
        m_transfers->EnsureVisible(index);
        using Marconi::Files::TransferState;
        bool finished = status.state == TransferState::Done || status.state == TransferState::Skipped ||
                        status.state == TransferState::Failed || status.state == TransferState::Cancelled;
        if (finished)
        {
            auto refresh = m_refreshWhenDone.find(status.id);
            if (refresh != m_refreshWhenDone.end())
            {
                refresh->second->Refresh(); // once, when the copy's last item finishes
                m_refreshWhenDone.erase(refresh);
            }
        }
    }

    Marconi::Files::ConflictAnswer FilePanel::AskAboutExisting(const std::string& destinationPath)
    {
        // On the transfer queue's thread: ask on the UI thread and wait, giving up if the window closes.
        auto answer = std::make_shared<std::promise<Marconi::Files::ConflictAnswer>>();
        std::future<Marconi::Files::ConflictAnswer> future = answer->get_future();
        CallAfter([this, answer, destinationPath]() {
            wxRichMessageDialog dialog(
                this,
                "\"" + wxString::FromUTF8(Marconi::Files::FileName(destinationPath)) +
                    "\" already exists in " + wxString::FromUTF8(Marconi::Files::ParentPath(destinationPath)) + ".",
                "File already exists", wxYES_NO | wxCANCEL | wxICON_QUESTION);
            dialog.SetYesNoCancelLabels("Replace", "Keep Both", "Skip");
            dialog.ShowCheckBox("Do this for the rest of this copy");
            int result = dialog.ShowModal();
            Marconi::Files::ConflictAnswer reply;
            reply.choice = result == wxID_YES  ? Marconi::Files::Conflict::Replace
                           : result == wxID_NO ? Marconi::Files::Conflict::KeepBoth
                                               : Marconi::Files::Conflict::Skip;
            reply.forTheRest = dialog.IsCheckBoxChecked();
            answer->set_value(reply);
        });
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
        {
            if (m_closing)
            {
                return {Marconi::Files::Conflict::Skip, true};
            }
        }
        return future.get();
    }
} // namespace MarconiUI
