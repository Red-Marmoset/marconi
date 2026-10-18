// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The file transfer panel: this computer's files beside the remote computer's, with copying both ways

#include "FilePanel.h"

#include "Appearance.h"
#include "ClipboardFiles.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"
#include "FilePane.h"

#include <Log/Log.h>

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textdlg.h>
#include <wx/utils.h>
#include <wx/volume.h>

#include <MarconiCore/Files/FileView.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <random>

namespace MarconiUI
{
    namespace
    {
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
            SetMessage("Wait for the current copy to finish before copying files out.");
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

    void FilePanel::CopyToClipboard(bool fromRemote, const std::vector<std::string>& paths)
    {
        if (paths.empty())
        {
            return;
        }
        std::vector<std::string> files = fromRemote ? DownloadForDrag(paths) : paths;
        if (files.empty())
        {
            return;
        }
        if (!SetClipboardFiles(files))
        {
            SetMessage("Could not put the files on the clipboard.");
            return;
        }
        wxString count = wxString::Format("%zu item%s", files.size(), files.size() == 1 ? "" : "s");
        SetMessage(fromRemote ? "Copied " + count + ". Paste them in Explorer or Finder, or anywhere files go."
                              : "Copied " + count + ". Paste them on the other side, or on the remote screen.");
    }

    void FilePanel::PasteFromClipboard()
    {
        std::vector<std::string> files = GetClipboardFiles();
        if (files.empty())
        {
            SetMessage("The clipboard holds no files to paste.");
            return;
        }
        if (m_state != State::Connected)
        {
            SetMessage("Not connected for file transfer yet.");
            return;
        }
        CopyBetweenSides(false, files);
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
                refresh->second->Reload(); // once, when the copy's last item finishes
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
