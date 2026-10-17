// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The file transfer panel: this computer's files beside the remote computer's, with copying both ways

#pragma once

#include <MarconiCore/Files/LocalFiles.h>
#include <MarconiCore/Files/SftpFiles.h>
#include <MarconiCore/Files/TightFiles.h>
#include <MarconiCore/Files/TransferQueue.h>

#include <wx/panel.h>

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class wxButton;
class wxListView;
class wxStaticText;

namespace MarconiUI
{
    class FilePane;

    class FilePanel : public wxPanel
    {
    public:
        struct Remote
        {
            std::string host;
            uint16_t port = 22;
            std::string username;
            std::string password;
            wxString displayName; // e.g. the desktop name, for headings and prompts
            wxString savedId;     // the saved connection, to remember a trusted host key
            wxString trustedKey;  // the host key fingerprint trusted before, if any
            wxString dropFolder;  // TightVNC: where files dropped on the remote screen go; asked for if empty
        };

        FilePanel(wxWindow* parent, const Remote& remote);
        ~FilePanel() override;

        // Connects to the remote computer the first time the panel is shown.
        void Activate();
        // The VNC connection offers TightVNC file transfer (or no longer does, with nullptr): use it instead of SFTP.
        void SetTightFiles(Marconi::Files::TightFiles* files, const wxString& displayName);
        // Files dropped on the remote screen: copy them to the remote Desktop (connecting first if needed).
        void UploadToDesktop(const std::vector<std::string>& localPaths);
        bool IsBusy() const;
        // Stops transfers and the connection; call before the window closes.
        void Shutdown();

        // Called by the panes: copy `paths` from one side into the other side's folder.
        void CopyBetweenSides(bool fromRemote, const std::vector<std::string>& paths);
        // Copy (Ctrl+C) in a pane: puts the files on the clipboard for pasting anywhere files go. Remote files are
        // downloaded to a temporary folder first (blocking, like a drag out).
        void CopyToClipboard(bool fromRemote, const std::vector<std::string>& paths);
        // Paste (Ctrl+V) in the remote pane: copies the files on the clipboard into the remote pane's folder.
        void PasteFromClipboard();
        // Remote files dragged out to Explorer or Finder: downloads them to a temporary folder (blocking) and
        // returns the local copies, which the drop then copies to where the user dropped them.
        std::vector<std::string> DownloadForDrag(const std::vector<std::string>& remotePaths);

    private:
        enum class State
        {
            Idle,
            Connecting,
            Connected,
            Failed,
        };

        void Connect();
        void HandleConnected(bool ok, const std::string& error);
        void Login();
        void HandleLoggedIn(bool ok, const std::string& error);
        void SetMessage(const wxString& message);
        void StartCopy(Marconi::Files::FileSource& from, const std::vector<std::string>& paths,
                       Marconi::Files::FileSource& to, const std::string& destination, FilePane* refreshAfter);
        void ShowStatus(const Marconi::Files::TransferStatus& status);
        Marconi::Files::ConflictAnswer AskAboutExisting(const std::string& destinationPath);
        void JoinWorker();
        Marconi::Files::FileSource* GetRemote();
        void ShowRemote(Marconi::Files::FileSource* source, const std::string& folder);

        Remote m_remote;
        Marconi::Files::LocalFiles m_local;
        std::unique_ptr<Marconi::Files::SftpFiles> m_sftp;
        Marconi::Files::TightFiles* m_tight = nullptr; // owned by the viewer
        std::unique_ptr<Marconi::Files::TransferQueue> m_queue;
        State m_state = State::Idle;
        std::thread m_worker; // connects and logs in
        std::atomic<bool> m_closing{false};
        std::vector<std::string> m_pendingDesktopUploads; // dropped before the connection was ready

        FilePane* m_localPane = nullptr;
        FilePane* m_remotePane = nullptr;
        wxStaticText* m_message = nullptr;
        wxString m_messageText;
        wxButton* m_upload = nullptr;
        wxButton* m_download = nullptr;
        wxButton* m_cancel = nullptr;
        wxListView* m_transfers = nullptr;
        std::map<int, long> m_transferRows; // status id -> row
        std::map<int, FilePane*> m_refreshWhenDone;
        std::vector<std::string> m_dragFolders; // temporary copies for drags out; removed when the panel goes
    };
} // namespace MarconiUI
