// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The file transfer panel: this computer's files beside the remote computer's, with copying both ways

#pragma once

#include <MarconiCore/Files/LocalFiles.h>
#include <MarconiCore/Files/SftpFiles.h>
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
        };

        FilePanel(wxWindow* parent, const Remote& remote);
        ~FilePanel() override;

        // Connects to the remote computer the first time the panel is shown.
        void Activate();
        // Files dropped on the remote screen: copy them to the remote Desktop (connecting first if needed).
        void UploadToDesktop(const std::vector<std::string>& localPaths);
        bool IsBusy() const;
        // Stops transfers and the connection; call before the window closes.
        void Shutdown();

        // Called by the panes: copy `paths` from one side into the other side's folder.
        void CopyBetweenSides(bool fromRemote, const std::vector<std::string>& paths);

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

        Remote m_remote;
        Marconi::Files::LocalFiles m_local;
        std::unique_ptr<Marconi::Files::SftpFiles> m_sftp;
        std::unique_ptr<Marconi::Files::TransferQueue> m_queue;
        State m_state = State::Idle;
        std::thread m_worker; // connects and logs in
        std::atomic<bool> m_closing{false};
        std::vector<std::string> m_pendingDesktopUploads; // dropped before the connection was ready

        FilePane* m_localPane = nullptr;
        FilePane* m_remotePane = nullptr;
        wxStaticText* m_message = nullptr;
        wxButton* m_upload = nullptr;
        wxButton* m_download = nullptr;
        wxButton* m_cancel = nullptr;
        wxListView* m_transfers = nullptr;
        std::map<int, long> m_transferRows; // status id -> row
        std::map<int, FilePane*> m_refreshWhenDone;
    };
} // namespace MarconiUI
