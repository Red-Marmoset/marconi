// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include "ClipboardSync.h"
#include "VncCanvas.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Net/Probe.h>
#include <MarconiCore/Rfb/Client.h>

#include <wx/frame.h>
#include <wx/image.h>
#include <wx/timer.h>

class wxMenu;
class wxToolBar;

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace MarconiUI
{
    struct ViewerOptions
    {
        Marconi::Address address;
        Marconi::Rfb::Credentials credentials;
        ScaleMode scaleMode = ScaleMode::Fit;
        bool viewOnly = false;
        bool fullScreen = false;
        bool clipboard = true;
        wxString savedId;       // the saved connection this window was opened from, if any
        wxString detected;      // from the saved connection: what the last probe found, e.g. "vnc 5900"
        bool skipProbe = false; // the address's protocol and port are already known (e.g. found on the network)
        std::vector<uint8_t> trustedCertificateSha256; // a server certificate the user accepted before

        // Headless (testing and automation): no window and no dialogs. Saves the picture the viewer would show
        // to the log folder every `snapshotEverySeconds`, and closes after `quitAfterSeconds`.
        bool headless = false;
        int snapshotEverySeconds = 5;
        int quitAfterSeconds = 60;

        // Diagnostics (headless experiments against misbehaving servers).
        std::vector<int32_t> encodings;        // sent exactly as given, pseudo-encodings included; empty = defaults
        bool resendPixelFormatOnResize = true; // repeat SetPixelFormat after a resize
        bool recovery = true;                  // re-request or reconnect when no picture arrives
    };

    struct SavedConnection;
    class ConnectionStore;

    // Viewer options for a saved connection, including its keychain password. False (with `error`) if its address
    // can't be parsed.
    bool ViewerOptionsFromSaved(const SavedConnection& connection, ConnectionStore& store, ViewerOptions& out,
                                std::string& error);

    class ViewerFrame : public wxFrame, private Marconi::Rfb::ClientListener
    {
    public:
        explicit ViewerFrame(const ViewerOptions& options);
        ~ViewerFrame() override;

    private:
        // ClientListener (connection thread)
        void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override;
        void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override;
        void OnDesktopResized(int width, int height) override;
        void OnBell() override;
        void OnCursorShape(const Marconi::Rfb::CursorShape& cursor) override;
        void OnServerCutText(const std::string& latin1Text) override;
        void OnDisconnected(const Marconi::Rfb::DisconnectInfo& info) override;
        bool OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials) override;
        bool OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate) override;

        // Connection thread: runs `question` on the UI thread (it shows a dialog and returns the answer) and waits
        // for the answer, or for the window to close. The question must keep its results in members, not in the
        // calling thread's locals, since that thread may have moved on if the window closed meanwhile.
        bool AskUser(const std::function<bool()>& question);

        // UI thread
        void RunPendingQuestion();
        bool ConfirmCertificate(const Marconi::Net::TlsCertificate& certificate);
        void Connect();
        void StartProbe();
        void StopProbe();
        void HandleProbe(const std::vector<Marconi::Net::ProbeResult>& results);
        void ConnectVnc(uint16_t port);
        void RememberDetected(const wxString& detected);
        void HandleConnected(const Marconi::Rfb::ServerInit& serverInit);
        void HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info);
        void SaveSnapshot();
        void WatchClipboard();
        void SaveHeadlessSnapshot();
        void SavePngInBackground(const wxImage& image, const wxString& path);
        void StopTimers();
        void FitToDesktop();
        void OnClose(wxCloseEvent& event);
        wxString GetServerDisplayName() const;

        // Toolbar / F8 menu
        void CreateToolbar();
        wxMenu* CreateMenu() const;
        void ShowMenu();
        void OnCommand(wxCommandEvent& event);
        void UpdateControls();
        void UpdateStatus();
        void SendKeyCombination(std::initializer_list<uint32_t> keysyms);
        void ToggleFullScreen();
        void SyncClipboardToServer();
        void SaveSettings();
        void WaitForPicture();
        void CheckForPicture();
        void LogTraffic();

        ViewerOptions m_options;
        Marconi::Rfb::Client m_client;
        ClipboardSync m_clipboard{m_client};
        VncCanvas* m_canvas = nullptr;
        wxToolBar* m_toolBar = nullptr;
        wxString m_desktopName;
        wxSize m_connectedSize;
        bool m_connected = false;
        wxTimer m_pictureTimer;   // re-asks for the desktop if a fresh connection still shows nothing
        int m_pictureRetries = 0;
        bool m_waitingForPicture = false; // no pixels since connecting or the last resize
        int m_autoReconnects = 0;         // reconnects made because the server stopped sending pixels
        bool m_autoReconnecting = false;
        wxTimer m_statsTimer;     // logs traffic every 10 seconds
        wxTimer m_snapshotTimer;  // saves the framebuffer to the log folder a little after a resize
        int m_snapshots = 0;
        wxTimer m_headlessSnapshotTimer;
        wxTimer m_quitTimer;
        int m_headlessSnapshots = 0;
        std::vector<std::thread> m_saveThreads; // PNG encoders, joined on destruction
        wxTimer m_clipboardTimer; // sends local clipboard changes while connected
        long m_clipboardChangeCount = -1;
        bool m_rememberPassword = false; // the user ticked "remember" at the prompt: save once logged in
        std::thread m_probeThread;       // finds which protocol the machine offers, before connecting
        std::atomic<bool> m_probeCancel{false};
        bool m_usingRemembered = false; // connecting with ViewerOptions::detected, not a fresh probe
        bool m_reprobed = false;        // a remembered result failed and we probed again; don't loop
        uint16_t m_port = 0;            // the port we connect to

        // Hand-off between the connection thread (waiting for an answer) and the UI thread (asking the user), for
        // credentials and for certificates this computer doesn't trust.
        std::mutex m_questionMutex;
        std::condition_variable m_questionChanged;
        std::function<bool()> m_question; // waiting to run on the UI thread
        bool m_questionAnswered = false;
        bool m_questionAccepted = false;
        Marconi::Rfb::Credentials m_credentialsAnswer; // what the credentials prompt returned
        bool m_closing = false; // guarded by m_questionMutex
    };
} // namespace MarconiUI
