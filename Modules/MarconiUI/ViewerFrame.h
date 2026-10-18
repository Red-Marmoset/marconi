// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include "ClipboardSync.h"
#include "ConnectionLostBar.h"
#include "FullScreenBar.h"
#include "SystemKeys.h"
#include "VncCanvas.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Files/TightFiles.h>
#include <MarconiCore/Net/Probe.h>
#include <MarconiCore/Quality.h>
#include <MarconiCore/Rfb/Client.h>

#include <wx/aui/framemanager.h>
#include <wx/frame.h>
#include <wx/image.h>
#include <wx/timer.h>

class wxAuiToolBar;

namespace MarconiUI
{
    class FilePanel;
}
class wxMenu;

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        class Client;
    }
} // namespace Marconi

namespace MarconiUI
{
    struct ViewerOptions
    {
        Marconi::Address address;
        Marconi::Rfb::Credentials credentials;
        ScaleMode scaleMode = ScaleMode::Fit;
        int zoomPercent = 100; // with ScaleMode::Fixed: one of Marconi::Zoom::kLevels
        bool viewOnly = false;
        bool fullScreen = false;
        bool clipboard = true;
        bool systemKeys = false; // send system shortcuts (Alt+Tab, the Windows key, Cmd+Tab) to the other computer
        long displayId = 0;     // which of the server's displays to show (its id), 0 for all of them
        Marconi::PictureQuality quality = Marconi::PictureQuality::Balanced;
        bool lockOnDisconnect = false; // send the lock shortcut to the other computer when the window closes
        wxString macAddress;    // for waking the computer up when it doesn't answer
        wxRect windowRect;      // where the window was last time; empty = fit it to the desktop
        wxString savedId;       // the saved connection this window was opened from, if any
        wxString detected;      // from the saved connection: what the last probe found, e.g. "vnc 5900"
        wxString layout;        // from the saved connection: where the toolbar and panels were
        wxString sshHostKey;    // from the saved connection: the SSH host key trusted for file transfer
        wxString dropFolder;    // from the saved connection: where files dropped on a TightVNC screen go
        bool showFiles = false; // open the file panel straight away (--files)
        bool filesCheck = false; // headless: copy a small file to the server and back over TightVNC, then quit
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

    class ViewerFrame : public wxFrame, private Marconi::SessionListener
    {
    public:
        explicit ViewerFrame(const ViewerOptions& options);
        ~ViewerFrame() override;

    private:
        // SessionListener (connection thread)
        void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override;
        void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override;
        void OnDesktopResized(int width, int height) override;
        void OnScreenLayout(const std::vector<Marconi::Rfb::Screen>& screens) override;
        void OnDesktopResizeRefused(int status) override;
        void OnBell() override;
        void OnCursorShape(const Marconi::Rfb::CursorShape& cursor) override;
        void OnServerCutText(const std::string& latin1Text) override;
        void OnServerClipboard(const Marconi::Rfb::ClipboardContent& content) override;
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
        void ConnectMarconi(uint16_t port);
        // The VNC connection object, made on the first VNC connect and kept for the window's life (reconnects reuse
        // it); m_session points at it afterwards.
        Marconi::Rfb::Client& EnsureRfbClient();
        Marconi::Wire::Client& EnsureWireClient();
        void RememberDetected(const wxString& detected);
        void HandleConnected(const Marconi::Rfb::ServerInit& serverInit);
        void HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info);
        void WatchClipboard();
        void SaveHeadlessSnapshot();
        // `done` (optional) runs on the UI thread with whether the file was written.
        void SavePngInBackground(const wxImage& image, const wxString& path, std::function<void(bool)> done = nullptr);
        wxMenu* CreateScreenshotMenu() const;
        wxImage TakeScreenshot() const;
        void SaveScreenshot();
        void CopyScreenshot();
        static bool IncludePointerInScreenshots();
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
        wxMenu* CreateSendKeysMenu() const;
        void UpdateSystemKeyCapture();
        void ToggleFullScreen();
        void ToggleToolbar();
        void ToggleFiles();
        void ShowFiles();
        void ApplyLayout(const wxString& layout);
        wxString GetLayout() const;
        void SyncClipboardToServer();
        void ApplyDisplayChoice();
        void RequestMatchingSize();
        void UploadToRemoteDesktop(const std::vector<std::string>& paths);
        void PasteFilesFromClipboard();
        void SaveSettings();
        void WaitForPicture();
        void CheckForPicture();
        // A connection that was up and went away: keep the window, dim the picture, retry on a schedule.
        void EnterLostState(const std::string& error);
        void EnterWakeState();
        void ScheduleRetry(const std::string& error);
        void OnRetryTick();
        void RetryNow();
        void LeaveLostState();
        void LogTraffic();

        ViewerOptions m_options;
        // The connection, whatever protocol it speaks: null until the first connect. The canvas and the clipboard
        // sync hold the same pointer. m_rfb is the same object when it is a VNC connection, for VNC-only features
        // (TightVNC file transfer, Mac quirks); null otherwise.
        std::unique_ptr<Marconi::RemoteSession> m_session;
        Marconi::Rfb::Client* m_rfb = nullptr;
        Marconi::Wire::Client* m_wire = nullptr;
        std::function<void()> m_reconnect; // reconnects with the protocol this window is using (VNC or Marconi)
        ClipboardSync m_clipboard;
        VncCanvas* m_canvas = nullptr;
        wxAuiManager m_aui;               // docks the toolbar (and later the file panel) around the screen
        wxAuiToolBar* m_toolBar = nullptr;
        FilePanel* m_files = nullptr;
        std::unique_ptr<Marconi::Files::TightFiles> m_tightFiles; // file transfer on the VNC connection (TightVNC)
        bool m_allowTight = true; // cleared after a TightVNC login goes wrong, so the retry uses the plain login
        bool m_allowEncryption = true; // cleared when the server only offers encryption this computer can't make
        wxString m_defaultLayout;         // the layout before any saved one was applied
        wxString m_layoutBeforeFullScreen; // full screen hides docked panels; this brings them back
        wxString m_desktopName;
        wxSize m_connectedSize;
        bool m_connected = false;
        ConnectionLostBar* m_lostBar = nullptr;
        FullScreenBar* m_fullScreenBar = nullptr; // appears at the top edge in full screen
        std::vector<Marconi::Rfb::Screen> m_screens; // the server's displays, when it describes them
        long m_displayId = 0;         // the display shown, 0 for all
        bool m_matchWindowSize = false; // ask the server to resize its screen to this window
        wxTimer m_matchSizeTimer;      // settles window resizes before asking
        bool m_reconnecting = false;   // the connection was lost and attempts are being made (or waited for)
        bool m_retryUnderWay = false;  // an attempt is connecting right now
        int m_retryAttempt = 0;
        std::string m_lostError;
        wxString m_lostHeadline;       // "Connection lost" or "Sent a wake-up call"
        bool m_windowRestored = false; // the window took its saved place and size, so isn't fitted to the desktop
        Marconi::PictureQuality m_quality = Marconi::PictureQuality::Balanced;
        wxTimer m_retryTimer;          // ticks every second while waiting for the next attempt
        std::chrono::steady_clock::time_point m_lostAt;
        std::chrono::steady_clock::time_point m_retryDue;
        bool m_active = false;          // the window is the active one (system shortcuts are only caught then)
        bool m_sendSystemKeys = false;  // the user wants Alt+Tab, the Windows key and the like sent to the server
        std::unique_ptr<SystemKeyCapture> m_systemKeys;
        wxTimer m_pictureTimer;   // re-asks for the desktop if a fresh connection still shows nothing
        int m_pictureRetries = 0;
        bool m_waitingForPicture = false; // no pixels since connecting or the last resize
        int m_autoReconnects = 0;         // reconnects made because the server stopped sending pixels
        bool m_autoReconnecting = false;
        wxTimer m_statsTimer;     // logs traffic every 10 seconds
        wxTimer m_headlessSnapshotTimer;
        wxTimer m_quitTimer;
        int m_headlessSnapshots = 0;
        std::vector<std::thread> m_saveThreads; // PNG encoders, joined on destruction
        wxString m_screenshotFolder;            // where the last screenshot of this window went
        wxTimer m_clipboardTimer; // sends local clipboard changes while connected and in front
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
