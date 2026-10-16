// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The file transfer panel: this computer's files beside the remote computer's, with copying both ways

#include "FilePanel.h"

#include "Appearance.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"

#include <Log/Log.h>

#include <wx/button.h>
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

#include <algorithm>
#include <chrono>

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
            return entry.isFolder ? wxString("Folder") : wxFileName::GetHumanReadableSize(wxULongLong(entry.size));
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

    // One side: a heading, the folder path, Up and Refresh, and the folder's contents.
    class FilePane : public wxPanel
    {
    public:
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
            wxButton* up = new wxButton(this, wxID_ANY, "Up", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            wxButton* refresh = new wxButton(this, wxID_ANY, "Refresh", wxDefaultPosition, wxDefaultSize,
                                             wxBU_EXACTFIT);
            m_path = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                    wxTE_PROCESS_ENTER);
            bar->Add(up, 0, wxRIGHT, FromDIP(4));
            bar->Add(m_path, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            bar->Add(refresh, 0);
            sizer->Add(bar, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
            m_list = new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(160, 120)),
                                    wxLC_REPORT | wxBORDER_SIMPLE);
            m_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(150));
            m_list->AppendColumn("Size", wxLIST_FORMAT_RIGHT, FromDIP(70));
            m_list->AppendColumn("Modified", wxLIST_FORMAT_LEFT, FromDIP(115));
            sizer->Add(m_list, 1, wxEXPAND);
            SetSizer(sizer);

            up->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoUp(); });
            refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Refresh(); });
            m_path->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Navigate(m_path->GetValue().utf8_string()); });
            m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { OnActivated(event); });
            m_list->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent&) { StartDrag(); });
            m_list->Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent&) { ShowContextMenu(); });
            m_list->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) { OnKey(event); });
            m_list->SetDropTarget(new DropTarget(*this));
            Enable(false);
        }

        bool IsRemote() const { return m_remote; }
        const std::string& GetFolder() const { return m_folder; }
        Marconi::Files::FileSource* GetSource() const { return m_source; }

        void SetSource(Marconi::Files::FileSource* source, const std::string& folder)
        {
            m_source = source;
            Enable(source != nullptr);
            if (source != nullptr)
            {
                Navigate(folder);
            }
        }

        void SetHeading(const wxString& heading) { m_heading->SetLabel(heading); }

        void Navigate(const std::string& folder)
        {
            if (m_source == nullptr)
            {
                return;
            }
            std::vector<Marconi::Files::FileEntry> entries;
            std::string error;
            if (!m_source->List(folder, entries, error))
            {
                wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
                m_path->ChangeValue(wxString::FromUTF8(m_folder));
                return;
            }
            // Folders first, then by name, ignoring case; hidden files (".name") last.
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                if (a.isFolder != b.isFolder)
                {
                    return a.isFolder;
                }
                bool aHidden = !a.name.empty() && a.name[0] == '.';
                bool bHidden = !b.name.empty() && b.name[0] == '.';
                if (aHidden != bHidden)
                {
                    return bHidden;
                }
                return wxString::FromUTF8(a.name).CmpNoCase(wxString::FromUTF8(b.name)) < 0;
            });
            m_folder = folder;
            m_entries = entries;
            m_path->ChangeValue(wxString::FromUTF8(folder));
            m_list->Freeze();
            m_list->DeleteAllItems();
            for (size_t i = 0; i < m_entries.size(); ++i)
            {
                const auto& entry = m_entries[i];
                long row = m_list->InsertItem(static_cast<long>(i), wxString::FromUTF8(entry.name));
                m_list->SetItem(row, 1, FormatSize(entry));
                m_list->SetItem(row, 2, FormatModified(entry.modified));
            }
            m_list->Thaw();
        }

        void Refresh()
        {
            if (!m_folder.empty())
            {
                Navigate(m_folder);
            }
        }

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

        // Paths dropped on this pane: from the other pane (`fromOtherPane`) or from Explorer/Finder.
        void OnDropped(const std::vector<std::string>& paths, bool fromRemote)
        {
            if (fromRemote == m_remote)
            {
                return; // dropped back where it came from
            }
            m_owner.CopyBetweenSides(fromRemote, paths);
        }

    private:
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

        void GoUp()
        {
            if (!m_folder.empty())
            {
                Navigate(Marconi::Files::ParentPath(m_folder));
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
        wxStaticText* m_heading = nullptr;
        wxTextCtrl* m_path = nullptr;
        wxListView* m_list = nullptr;
    };

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
        sizer->Add(panes, 3, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

        wxBoxSizer* transfersBar = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* transfersHeading = new wxStaticText(this, wxID_ANY, "Transfers");
        transfersHeading->SetFont(transfersHeading->GetFont().Bold());
        m_cancel = new wxButton(this, wxID_ANY, "Cancel All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        transfersBar->Add(transfersHeading, 1, wxALIGN_CENTER_VERTICAL);
        transfersBar->Add(m_cancel, 0);
        sizer->Add(transfersBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
        m_transfers = new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 60)),
                                     wxLC_REPORT | wxLC_NO_HEADER | wxBORDER_SIMPLE);
        m_transfers->SetMinSize(FromDIP(wxSize(-1, 60)));
        m_transfers->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(260));
        m_transfers->AppendColumn("Progress", wxLIST_FORMAT_LEFT, FromDIP(260));
        sizer->Add(m_transfers, 1, wxEXPAND | wxALL, FromDIP(6));
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

        m_localPane->SetSource(&m_local, m_local.GetHome());
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

    void FilePanel::Shutdown()
    {
        m_closing = true;
        if (m_sftp)
        {
            m_sftp->Abort();
        }
        m_queue.reset(); // cancels and waits for the copy in progress
        JoinWorker();
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
        if (m_state == State::Idle || m_state == State::Failed)
        {
            Connect();
        }
    }

    void FilePanel::Connect()
    {
        if (m_remote.host.empty())
        {
            SetMessage("There's no computer to connect to.");
            return;
        }
        m_state = State::Connecting;
        SetMessage("Connecting to " + m_remote.displayName + " for file transfer" + wxString::FromUTF8("\xE2\x80\xA6"));
        JoinWorker();
        m_sftp = std::make_unique<Marconi::Files::SftpFiles>();
        Marconi::Files::SftpFiles* sftp = m_sftp.get();
        std::string host = m_remote.host;
        uint16_t port = m_remote.port;
        m_worker = std::thread([this, sftp, host, port]() {
            std::string error;
            bool ok = sftp->Connect(host, port, 10000, error);
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
        if (!m_remote.trustedKey.empty() && m_remote.trustedKey != fingerprint)
        {
            m_state = State::Failed;
            m_sftp->Abort();
            LOG_WARNING("Files", "SSH host key for %s changed: was %s, now %s", m_remote.host.c_str(),
                        m_remote.trustedKey.utf8_string().c_str(), key.fingerprint.c_str());
            SetMessage("Refused: " + m_remote.displayName + " didn't identify itself the way it did before.");
            wxMessageBox(m_remote.displayName + " identified itself differently from last time.\n\nThis happens "
                         "when the computer has been reinstalled, but it can also mean another computer is "
                         "pretending to be it. Marconi won't send your password to it.\n\nWas: " +
                             m_remote.trustedKey + "\nNow: " + fingerprint,
                         "Marconi", wxOK | wxICON_WARNING, this);
            return;
        }
        if (m_remote.trustedKey.empty())
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
        m_state = State::Connected;
        SetMessage("");
        m_remotePane->SetHeading(m_remote.displayName);
        m_remotePane->SetSource(m_sftp.get(), m_sftp->GetHome());
        m_upload->Enable(true);
        m_download->Enable(true);
        if (!m_pendingDesktopUploads.empty())
        {
            std::vector<std::string> pending;
            pending.swap(m_pendingDesktopUploads);
            UploadToDesktop(pending);
        }
    }

    void FilePanel::CopyBetweenSides(bool fromRemote, const std::vector<std::string>& paths)
    {
        if (paths.empty() || m_state != State::Connected)
        {
            return;
        }
        FilePane* destination = fromRemote ? m_localPane : m_remotePane;
        StartCopy(fromRemote ? static_cast<Marconi::Files::FileSource&>(*m_sftp)
                             : static_cast<Marconi::Files::FileSource&>(m_local),
                  paths,
                  fromRemote ? static_cast<Marconi::Files::FileSource&>(m_local)
                             : static_cast<Marconi::Files::FileSource&>(*m_sftp),
                  destination->GetFolder(), destination);
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
        // The remote Desktop folder: neither VNC nor SSH says which window is under the pointer.
        std::string desktop = Marconi::Files::JoinPath(m_sftp->GetHome(), "Desktop");
        Marconi::Files::FileEntry entry;
        std::string error;
        if (!m_sftp->Stat(desktop, entry, error) || !entry.isFolder)
        {
            desktop = m_sftp->GetHome();
        }
        StartCopy(m_local, localPaths, *m_sftp, desktop, nullptr);
        SetMessage(wxString::Format("Copying %zu item%s to ", localPaths.size(), localPaths.size() == 1 ? "" : "s") +
                   wxString::FromUTF8(desktop));
    }

    void FilePanel::StartCopy(Marconi::Files::FileSource& from, const std::vector<std::string>& paths,
                              Marconi::Files::FileSource& to, const std::string& destination, FilePane* refreshAfter)
    {
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
                refresh->second->Refresh();
                m_refreshWhenDone.erase(refresh);
            }
            else
            {
                m_remotePane->Refresh(); // e.g. a drop on the remote screen
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
