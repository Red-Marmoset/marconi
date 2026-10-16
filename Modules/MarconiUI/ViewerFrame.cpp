// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#include "ViewerFrame.h"

#include "AppIcon.h"
#include "ClipboardWatch.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"
#include "FilePanel.h"
#include "ForegroundWindow.h"
#include "VncCanvas.h"

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/Net/ProtocolChoice.h>
#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <wx/datetime.h>
#include <wx/app.h>
#include <wx/aui/auibar.h>
#include <wx/display.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

namespace MarconiUI
{
    namespace
    {
        // "AB:CD:..." in two lines, as browsers show certificate fingerprints.
        wxString FormatFingerprint(const std::vector<uint8_t>& sha256)
        {
            wxString text;
            for (size_t i = 0; i < sha256.size(); ++i)
            {
                if (i > 0)
                {
                    text += i == sha256.size() / 2 ? "\n" : ":";
                }
                text += wxString::Format("%02X", sha256[i]);
            }
            return text;
        }

        enum CommandId
        {
            ID_SCALE_FIT = wxID_HIGHEST + 100,
            ID_SCALE_ACTUAL,
            ID_FULL_SCREEN,
            ID_VIEW_ONLY,
            ID_CLIPBOARD,
            ID_SEND_CTRL_ALT_DEL,
            ID_SEND_F8,
            ID_SEND_F9,
            ID_SEND_F10,
            ID_SHOW_TOOLBAR,
            ID_FILES,
            ID_REFRESH,
            ID_DISCONNECT,
        };
    } // namespace

    ViewerFrame::ViewerFrame(const ViewerOptions& options)
        : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(Marconi::FormatAddress(options.address)) + " - Marconi"),
          m_options(options),
          m_client(*this)
    {
        if (m_options.credentials.username.empty())
        {
            m_options.credentials.username = m_options.address.username;
        }
        SetIcons(GetAppIcons());
        m_canvas = new VncCanvas(this, m_client);
        m_aui.SetManagedWindow(this);
        m_aui.AddPane(m_canvas, wxAuiPaneInfo().Name("Screen").CenterPane());

        CreateToolbar();
        CreateStatusBar();
        FilePanel::Remote remote;
        remote.host = m_options.address.host;
        remote.username = m_options.credentials.username;
        remote.password = m_options.credentials.password;
        remote.displayName = wxString::FromUTF8(m_options.address.host);
        remote.savedId = m_options.savedId;
        remote.trustedKey = m_options.sshHostKey;
        m_files = new FilePanel(this, remote);
        m_aui.AddPane(m_files, wxAuiPaneInfo()
                                   .Name("Files")
                                   .Caption("Files")
                                   .Bottom()
                                   .BestSize(FromDIP(wxSize(900, 380)))
                                   .MinSize(FromDIP(wxSize(480, 300)))
                                   .Floatable(true)
                                   .Dockable(true)
                                   .Hide());
        m_canvas->SetFileDropHandler([this](const std::vector<std::string>& paths) {
            ShowFiles();
            m_files->UploadToDesktop(paths);
            SetStatusText(wxString::Format("Copying %zu item%s to the remote Desktop", paths.size(),
                                           paths.size() == 1 ? "" : "s"));
        });
        m_aui.Update();
        m_defaultLayout = m_aui.SavePerspective();
        if (!m_options.layout.empty())
        {
            ApplyLayout(m_options.layout);
        }
        if (m_options.showFiles && !m_options.headless)
        {
            CallAfter([this]() { ShowFiles(); });
        }
        else if (m_aui.GetPane("Files").IsShown() && !m_options.headless)
        {
            CallAfter([this]() { m_files->Activate(); });
        }
        SetClientSize(FromDIP(wxSize(800, 600)));

        m_canvas->SetLocalCommandHandler([this](Marconi::LocalCommand command) {
            if (command == Marconi::LocalCommand::Menu)
            {
                ShowMenu();
            }
            else if (command == Marconi::LocalCommand::Toolbar)
            {
                ToggleToolbar();
            }
            else if (command == Marconi::LocalCommand::Files)
            {
                ToggleFiles();
            }
        });
        m_canvas->SetScaleMode(options.scaleMode);
        m_canvas->SetViewOnly(options.viewOnly);
        m_clipboard.SetEnabled(options.clipboard);
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            event.Skip();
            if (event.GetActive())
            {
                // Windows gives an activated frame the focus itself, and keys typed at the frame go nowhere:
                // every activation must hand the keyboard back to the desktop.
                CallAfter([this]() { m_canvas->SetFocus(); });
                // Someone who copied something locally is probably about to paste it remotely.
                CallAfter([this]() { SyncClipboardToServer(); });
            }
        });
        Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
            event.Skip();
            CallAfter([this]() { m_canvas->SetFocus(); });
        });
        Bind(wxEVT_CLOSE_WINDOW, &ViewerFrame::OnClose, this);
        m_pictureTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { CheckForPicture(); }, m_pictureTimer.GetId());
        m_statsTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { LogTraffic(); }, m_statsTimer.GetId());
        m_clipboardTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { WatchClipboard(); }, m_clipboardTimer.GetId());
        Bind(wxEVT_MENU, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_TOOL, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            CallAfter([this]() { UpdateStatus(); }); // after the canvas has recomputed its scale
        });
        UpdateControls();
        if (m_options.headless)
        {
            LOG_INFO("Viewer", "Headless: snapshot every %ds, quitting after %ds", m_options.snapshotEverySeconds,
                     m_options.quitAfterSeconds);
            m_headlessSnapshotTimer.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) { SaveHeadlessSnapshot(); }, m_headlessSnapshotTimer.GetId());
            m_headlessSnapshotTimer.Start(std::max(m_options.snapshotEverySeconds, 1) * 1000);
            m_quitTimer.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                LOG_INFO("Viewer", "Headless session finished");
                Close(true);
            }, m_quitTimer.GetId());
            m_quitTimer.StartOnce(std::max(m_options.quitAfterSeconds, 1) * 1000);
        }
        Connect();
    }

    bool ViewerOptionsFromSaved(const SavedConnection& connection, ConnectionStore& store, ViewerOptions& out,
                                std::string& error)
    {
        ViewerOptions options;
        if (!Marconi::ParseAddress(connection.address.utf8_string(), options.address, error))
        {
            return false;
        }
        options.savedId = connection.id;
        options.detected = connection.detected;
        options.trustedCertificateSha256 = Marconi::HexToBytes(connection.certificate.utf8_string());
        options.credentials.username = connection.username.utf8_string();
        wxString password;
        if (connection.rememberPassword && store.LoadPassword(connection.id, password))
        {
            options.credentials.password = password.utf8_string();
        }
        options.scaleMode = connection.scaleMode;
        options.viewOnly = connection.viewOnly;
        options.clipboard = connection.clipboard;
        options.layout = connection.layout;
        options.sshHostKey = connection.sshHostKey;
        out = options;
        return true;
    }

    void ViewerFrame::SaveHeadlessSnapshot()
    {
        // What a viewer window would be showing right now, plus how that relates to the server's desktop.
        wxImage image = m_canvas->GetShownPicture();
        if (!image.IsOk())
        {
            LOG_INFO("Viewer", "Headless snapshot: nothing shown yet");
            return;
        }
        int serverWidth;
        int serverHeight;
        {
            Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            serverWidth = framebuffer.GetWidth();
            serverHeight = framebuffer.GetHeight();
        }
        ++m_headlessSnapshots;
        wxString path = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs" + wxFILE_SEP_PATH +
                        wxDateTime::Now().Format("headless_%Y-%m-%d_%H-%M-%S_") +
                        wxString::Format("%03d.png", m_headlessSnapshots);
        LOG_INFO("Viewer", "Headless snapshot %s: showing %dx%d (%s), server desktop %dx%d", path.utf8_string().c_str(),
                 image.GetWidth(), image.GetHeight(),
                 m_canvas->IsShowingCurrentPicture() ? "current" : "previous picture, new one incomplete", serverWidth,
                 serverHeight);
        SavePngInBackground(image, path);
    }

    void ViewerFrame::SavePngInBackground(const wxImage& image, const wxString& path)
    {
        // Encoding a 4K PNG takes seconds; doing it here would stall the viewer. The thread gets its own deep copy
        // (wxImage's reference counting isn't thread-safe) and is joined when the window goes away.
        wxImage copy = image.Copy();
        copy.SetOption(wxIMAGE_OPTION_PNG_COMPRESSION_LEVEL, 1);
        m_saveThreads.emplace_back([copy, path]() mutable {
            if (!copy.SaveFile(path, wxBITMAP_TYPE_PNG))
            {
                LOG_WARNING("Viewer", "Could not save %s", path.utf8_string().c_str());
            }
        });
    }

    void ViewerFrame::StopTimers()
    {
        m_pictureTimer.Stop();
        m_statsTimer.Stop();
        m_clipboardTimer.Stop();
        m_headlessSnapshotTimer.Stop();
        m_quitTimer.Stop();
    }

    ViewerFrame::~ViewerFrame()
    {
        m_aui.UnInit(); // before the panes it manages are destroyed
        StopProbe();
        for (std::thread& thread : m_saveThreads)
        {
            thread.join();
        }
        // Normally already done in OnClose; the Client must stop before this listener goes away.
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            m_closing = true;
        }
        m_questionChanged.notify_all();
        m_client.Disconnect();
    }

    wxString ViewerFrame::GetServerDisplayName() const
    {
        return m_desktopName.empty() ? wxString::FromUTF8(Marconi::FormatAddress(m_options.address)) : m_desktopName;
    }

    void ViewerFrame::Connect()
    {
        const Marconi::Address& address = m_options.address;
        long rememberedPort = 0;
        if (address.protocol == Marconi::Protocol::Vnc || m_options.skipProbe)
        {
            ConnectVnc(Marconi::EffectivePort(address));
        }
        else if (address.protocol == Marconi::Protocol::Auto && m_options.detected.StartsWith("vnc ") &&
                 m_options.detected.Mid(4).ToLong(&rememberedPort) && rememberedPort > 0 && rememberedPort < 65536)
        {
            m_usingRemembered = true;
            ConnectVnc(static_cast<uint16_t>(rememberedPort));
        }
        else
        {
            StartProbe();
        }
    }

    void ViewerFrame::ConnectVnc(uint16_t port)
    {
        m_port = port;
        m_clipboard.Reset();
        Marconi::Address shown = m_options.address;
        shown.port = port;
        shown.protocol = Marconi::Protocol::Vnc;
        SetStatusText("Connecting to " + wxString::FromUTF8(Marconi::FormatAddress(shown)) + "...");
        Marconi::Rfb::ConnectionOptions connection;
        connection.host = m_options.address.host;
        connection.port = port;
        connection.credentials = m_options.credentials;
        connection.trustedCertificateSha256 = m_options.trustedCertificateSha256;
        connection.encodings = m_options.encodings;
        connection.exactEncodings = !m_options.encodings.empty();
        connection.resendPixelFormatOnResize = m_options.resendPixelFormatOnResize;
        m_client.Connect(connection);
    }

    void ViewerFrame::StartProbe()
    {
        const wxString host = wxString::FromUTF8(m_options.address.host);
        SetStatusText("Looking for a remote desktop on " + host + wxString::FromUTF8("\xE2\x80\xA6")); // an ellipsis
        StopProbe();
        m_probeCancel = false;
        Marconi::Address address = m_options.address;
        m_probeThread = std::thread([this, address]() {
            std::vector<Marconi::Net::ProbeResult> results =
                Marconi::Net::Probe(address.host, Marconi::Net::DefaultProbeTargets(address), 1500, &m_probeCancel);
            if (!m_probeCancel)
            {
                CallAfter([this, results]() { HandleProbe(results); });
            }
        });
    }

    void ViewerFrame::StopProbe()
    {
        m_probeCancel = true;
        if (m_probeThread.joinable())
        {
            m_probeThread.join();
        }
    }

    void ViewerFrame::HandleProbe(const std::vector<Marconi::Net::ProbeResult>& results)
    {
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing)
            {
                return;
            }
        }
        for (const Marconi::Net::ProbeResult& result : results)
        {
            std::string detail = result.detail.empty() ? "" : " (" + result.detail + ")";
            LOG_INFO("Viewer", "Probe %s port %u: %s%s", m_options.address.host.c_str(), result.port,
                     result.reachable ? Marconi::ProtocolName(result.protocol) : "no answer", detail.c_str());
        }
        Marconi::Net::Choice choice = Marconi::Net::ChooseProtocol(m_options.address, results);
        if (choice.kind == Marconi::Net::Choice::Kind::Connect)
        {
            if (m_options.address.protocol == Marconi::Protocol::Auto && m_options.address.port == 0)
            {
                RememberDetected(wxString::Format("vnc %u", static_cast<unsigned>(choice.port)));
            }
            ConnectVnc(choice.port);
            return;
        }
        wxString message = wxString::FromUTF8(Marconi::Net::DescribeChoice(choice, m_options.address));
        LOG_WARNING("Viewer", "%s", message.utf8_string().c_str());
        SetStatusText(message);
        if (!m_options.headless)
        {
            wxMessageBox(message, "Marconi", wxOK | wxICON_INFORMATION, this);
        }
        Close(true);
    }

    void ViewerFrame::RememberDetected(const wxString& detected)
    {
        // Unlike the window settings, this is a fact about the machine, so headless runs keep it too.
        if (m_options.savedId.empty() || m_options.detected == detected)
        {
            return;
        }
        m_options.detected = detected;
        ConnectionStore store;
        SavedConnection saved;
        if (store.Load(m_options.savedId, saved))
        {
            saved.detected = detected;
            store.Save(saved);
        }
    }

    void ViewerFrame::OnConnected(const Marconi::Rfb::ServerInit& serverInit)
    {
        CallAfter([this, serverInit]() { HandleConnected(serverInit); });
    }

    void ViewerFrame::OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects)
    {
        CallAfter([this, rects]() {
            m_canvas->UpdateRects(rects);
            if (m_waitingForPicture && m_canvas->IsShowingCurrentPicture())
            {
                m_waitingForPicture = false;
                m_autoReconnects = 0;
                m_pictureTimer.Stop();
                UpdateStatus();
            }
        });
    }

    void ViewerFrame::OnDesktopResized(int width, int height)
    {
        CallAfter([this, width, height]() {
            // The window keeps its size (Fit mode scales): a Mac switches size whenever it locks or unlocks, and
            // the window jumping around with it is disorienting. It was fitted to the desktop on connecting.
            m_canvas->SetDesktopSize(width, height);
            WaitForPicture();
        });
    }

    void ViewerFrame::OnCursorShape(const Marconi::Rfb::CursorShape& cursor)
    {
        CallAfter([this, cursor]() { m_canvas->SetRemoteCursor(cursor); });
    }

    void ViewerFrame::OnServerCutText(const std::string& latin1Text)
    {
        CallAfter([this, latin1Text]() { m_clipboard.OnServerText(latin1Text); });
    }

    void ViewerFrame::SyncClipboardToServer()
    {
        // Only while the user is working in this window: what they copy in other programs (a password, say) isn't
        // meant for the remote computer. Coming back to the viewer sends the clipboard as it is then.
        if (m_connected && !m_canvas->IsViewOnly() && IsForegroundWindow(*this))
        {
            m_clipboard.SendIfChanged();
        }
    }

    void ViewerFrame::OnBell()
    {
        CallAfter([]() { wxBell(); });
    }

    void ViewerFrame::OnDisconnected(const Marconi::Rfb::DisconnectInfo& info)
    {
        CallAfter([this, info]() { HandleDisconnected(info); });
    }

    bool ViewerFrame::AskUser(const std::function<bool()>& question)
    {
        std::unique_lock<std::mutex> lock(m_questionMutex);
        if (m_closing)
        {
            return false;
        }
        m_question = question;
        m_questionAnswered = false;
        m_questionAccepted = false;
        CallAfter([this]() { RunPendingQuestion(); });
        m_questionChanged.wait(lock, [this]() { return m_questionAnswered || m_closing; });
        return !m_closing && m_questionAccepted;
    }

    void ViewerFrame::RunPendingQuestion()
    {
        std::function<bool()> question;
        {
            // The window may have started closing after this was queued; nobody is waiting any more.
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing || !m_question)
            {
                return;
            }
            question = std::move(m_question);
            m_question = nullptr;
        }
        bool accepted = question();
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            m_questionAccepted = accepted;
            m_questionAnswered = true;
        }
        m_questionChanged.notify_all();
    }

    bool ViewerFrame::OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials)
    {
        // Connection thread: ask the UI thread and wait for the answer (or for the window to close).
        if (m_options.headless)
        {
            LOG_WARNING("Viewer", "Headless: the server wants credentials that aren't saved for this connection");
            return false;
        }
        bool accepted = AskUser([this, kind]() {
            SetStatusText("Waiting for credentials...");
            bool offerRemember = !m_options.savedId.empty() && ConnectionStore::IsKeychainAvailable();
            CredentialsDialog dialog(this, GetServerDisplayName(), kind,
                                     wxString::FromUTF8(m_options.credentials.username), offerRemember);
            if (dialog.ShowModal() != wxID_OK)
            {
                m_rememberPassword = false;
                return false;
            }
            m_rememberPassword = dialog.GetRemember();
            m_credentialsAnswer.username = dialog.GetUsername().utf8_string();
            m_credentialsAnswer.password = dialog.GetPassword().utf8_string();
            // Keep them for the rest of this window's life so reconnects don't ask again.
            m_options.credentials = m_credentialsAnswer;
            SetStatusText("Authenticating...");
            return true;
        });
        if (!accepted)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_questionMutex);
        credentials = m_credentialsAnswer;
        return true;
    }

    bool ViewerFrame::OnUntrustedCertificate(const Marconi::Net::TlsCertificate& certificate)
    {
        // Connection thread.
        if (m_options.headless)
        {
            LOG_WARNING("Viewer", "Headless: the server's certificate isn't trusted (%s); not connecting",
                        certificate.problem.c_str());
            return false;
        }
        LOG_INFO("Viewer", "The server's certificate isn't trusted (%s); asking", certificate.problem.c_str());
        Marconi::Net::TlsCertificate copy = certificate; // the UI thread gets its own copy
        return AskUser([this, copy]() { return ConfirmCertificate(copy); });
    }

    bool ViewerFrame::ConfirmCertificate(const Marconi::Net::TlsCertificate& certificate)
    {
        SetStatusText("Waiting for you to check the server's certificate...");
        wxString message = wxString::Format(
            "%s identifies itself with a security certificate this computer doesn't trust (%s).\n\n"
            "Issued to: %s\nIssued by: %s\nValid until: %s\n\nFingerprint (SHA-256):\n%s\n\n"
            "If you set up that computer yourself, this is normal: connect anyway, and Marconi will remember this "
            "certificate for this connection. If you weren't expecting it, someone may be listening in: cancel and "
            "check the other computer.",
            wxString::FromUTF8(m_options.address.host), wxString::FromUTF8(certificate.problem),
            wxString::FromUTF8(certificate.subject), wxString::FromUTF8(certificate.issuer),
            wxString::FromUTF8(certificate.validUntil), FormatFingerprint(certificate.sha256));
        wxMessageDialog dialog(this, message, "Connection to " + GetServerDisplayName(),
                               wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        dialog.SetYesNoLabels("Connect Anyway", "Cancel");
        if (dialog.ShowModal() != wxID_YES)
        {
            return false;
        }
        // Reconnects in this window don't ask again, and neither does the saved connection next time.
        m_options.trustedCertificateSha256 = certificate.sha256;
        if (!m_options.savedId.empty())
        {
            ConnectionStore store;
            SavedConnection saved;
            if (store.Load(m_options.savedId, saved))
            {
                saved.certificate = wxString::FromUTF8(Marconi::BytesToHex(certificate.sha256));
                store.Save(saved);
            }
        }
        SetStatusText("Connecting securely...");
        return true;
    }

    void ViewerFrame::HandleConnected(const Marconi::Rfb::ServerInit& serverInit)
    {
        m_desktopName = wxString::FromUTF8(serverInit.name);
        SetTitle(GetServerDisplayName() + " - Marconi");
        m_connected = true;
        if (m_rememberPassword)
        {
            // Only now: the credentials are known to work.
            m_rememberPassword = false;
            ConnectionStore store;
            SavedConnection saved;
            if (store.Load(m_options.savedId, saved))
            {
                saved.username = wxString::FromUTF8(m_options.credentials.username);
                saved.rememberPassword = true;
                store.Save(saved);
                store.SavePassword(saved.id, saved.username, wxString::FromUTF8(m_options.credentials.password));
            }
        }
        m_canvas->SetDesktopSize(serverInit.width, serverInit.height);
        FitToDesktop();
        if (m_options.fullScreen && !IsFullScreen())
        {
            ToggleFullScreen();
        }
        UpdateControls();
        UpdateStatus();
        m_canvas->SetFocus();
        SyncClipboardToServer();
        WaitForPicture();
        m_client.TakeTrafficStats(); // start counting from here
        m_statsTimer.Start(10000);
        m_clipboardChangeCount = GetClipboardChangeCount();
        m_clipboardTimer.Start(500);
    }

    void ViewerFrame::WatchClipboard()
    {
        // Copying while the viewer is in front doesn't change focus, so watch for changes rather than waiting
        // for the window to be activated again. The count is recorded even when nothing is sent (another window
        // is in front), so a change made elsewhere is never sent later on its account: activation sends the
        // clipboard once instead.
        long count = GetClipboardChangeCount();
        if (count >= 0 && count != m_clipboardChangeCount)
        {
            m_clipboardChangeCount = count;
            SyncClipboardToServer();
        }
    }

    void ViewerFrame::LogTraffic()
    {
        // Shows whether the server keeps sending pictures, e.g. when the view freezes.
        Marconi::Rfb::TrafficStats stats = m_client.TakeTrafficStats();
        LOG_INFO("Viewer", "Last 10s: %d updates with %d rectangles, %d requests, %d pointer and %d key events sent",
                 stats.updates, stats.rectangles, stats.requests, stats.pointerEvents, stats.keyEvents);
    }

    void ViewerFrame::WaitForPicture()
    {
        // After connecting or a resize, until the server sends actual pixels.
        m_waitingForPicture = true;
        m_pictureRetries = 0;
        m_pictureTimer.Start(2000);
        UpdateStatus();
    }

    void ViewerFrame::CheckForPicture()
    {
        // macOS sometimes sends nothing but size changes when it switches display modes (e.g. at the lock screen)
        // or right after a previous session ended. Ask for the whole desktop again; if that doesn't help, reconnect,
        // since a fresh connection always gets a full picture.
        constexpr int kMaxRetries = 4;
        constexpr int kMaxAutoReconnects = 3;
        if (!m_connected || !m_waitingForPicture || !m_options.recovery)
        {
            m_pictureTimer.Stop();
            return;
        }
        if (m_pictureRetries < kMaxRetries)
        {
            ++m_pictureRetries;
            LOG_INFO("Viewer", "No picture from the server yet, asking for the whole desktop again (%d/%d)",
                     m_pictureRetries, kMaxRetries);
            m_client.RequestFullUpdate();
            return;
        }
        // Input sent during the reconnect would be lost (e.g. a password typed blind at the lock screen), and it
        // still reaches the server while the picture is stuck. Wait for a pause before reconnecting.
        constexpr std::chrono::seconds kQuietBeforeReconnect(3);
        if (m_client.GetTimeSinceInput() < kQuietBeforeReconnect)
        {
            LOG_INFO("Viewer", "Still no picture, but the user is typing or using the mouse; reconnecting later");
            m_client.RequestFullUpdate();
            return;
        }
        m_pictureTimer.Stop();
        if (m_autoReconnects >= kMaxAutoReconnects)
        {
            LOG_WARNING("Viewer", "Still no picture after %d reconnects; giving up", m_autoReconnects);
            SetStatusText("The server isn't sending a picture. Try Refresh, or reconnect later.");
            return;
        }
        ++m_autoReconnects;
        LOG_INFO("Viewer", "Still no picture; reconnecting (%d/%d)", m_autoReconnects, kMaxAutoReconnects);
        SetStatusText("The server stopped sending a picture; reconnecting...");
        m_autoReconnecting = true;
        m_client.Disconnect(); // OnDisconnected follows; HandleDisconnected reconnects
    }

    void ViewerFrame::HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info)
    {
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            if (m_closing)
            {
                return;
            }
        }
        LOG_INFO("Viewer", "Disconnected: %s", info.error.empty() ? "(no error)" : info.error.c_str());
        if (m_autoReconnecting)
        {
            m_autoReconnecting = false;
            m_connected = false;
            m_pictureTimer.Stop();
            m_statsTimer.Stop();
            m_clipboardTimer.Stop();
            ConnectVnc(m_port);
            return;
        }
        m_pictureTimer.Stop();
        m_statsTimer.Stop();
        m_clipboardTimer.Stop();
        m_canvas->ReleaseInput();
        m_connected = false;
        UpdateControls();
        if (!info.error.empty() && m_usingRemembered && !m_reprobed && !info.wasConnected &&
            !info.authenticationFailed)
        {
            // The machine may have changed (e.g. VNC turned off, RDP turned on): look again once.
            LOG_INFO("Viewer", "The remembered connection didn't answer; probing %s again",
                     m_options.address.host.c_str());
            m_reprobed = true;
            m_usingRemembered = false;
            RememberDetected(wxEmptyString);
            StartProbe();
            return;
        }
        if (info.error.empty() || m_options.headless)
        {
            Close(true);
            return;
        }

        wxString caption = "Connection to " + GetServerDisplayName();
        if (info.authenticationFailed)
        {
            SetStatusText("Authentication failed");
            wxMessageDialog retry(this, wxString::FromUTF8(info.error) + "\n\nWould you like to try again?", caption,
                                  wxYES_NO | wxICON_WARNING);
            retry.SetYesNoLabels("Try Again", "Close");
            if (retry.ShowModal() == wxID_YES)
            {
                m_options.credentials.password.clear(); // rejected: ask again
                ConnectVnc(m_port);
                return;
            }
            Close(true);
            return;
        }

        SetStatusText("Disconnected: " + wxString::FromUTF8(info.error));
        wxString message = info.wasConnected ? "The connection was lost:\n\n" : "Could not connect:\n\n";
        wxMessageDialog retry(this, message + wxString::FromUTF8(info.error), caption, wxYES_NO | wxICON_ERROR);
        retry.SetYesNoLabels(info.wasConnected ? "Reconnect" : "Try Again", "Close");
        if (retry.ShowModal() == wxID_YES)
        {
            ConnectVnc(m_port);
            return;
        }
        Close(true);
    }

    void ViewerFrame::FitToDesktop()
    {
        if (IsFullScreen() || IsMaximized())
        {
            return;
        }
        // Grow or shrink the window to show the whole desktop, limited to the display it's on.
        int displayIndex = wxDisplay::GetFromWindow(this);
        wxRect available = wxDisplay(displayIndex == wxNOT_FOUND ? 0u : static_cast<unsigned>(displayIndex)).GetClientArea();
        wxSize desktop = m_canvas->GetDesktopSize();
        wxSize decorations = GetSize() - m_canvas->GetSize(); // frame, status bar and any docked panels
        wxSize wanted = desktop + decorations;
        wanted.x = std::min(wanted.x, available.width);
        wanted.y = std::min(wanted.y, available.height);
        SetSize(wanted);
        wxRect frame = GetRect();
        if (!available.Contains(frame))
        {
            Move(std::max(available.x, std::min(frame.x, available.GetRight() - frame.width)),
                 std::max(available.y, std::min(frame.y, available.GetBottom() - frame.height)));
        }
    }

    void ViewerFrame::CreateToolbar()
    {
        // Text-only buttons in a panel that docks to any edge of the window or floats (F9 shows and hides it).
        m_toolBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxAUI_TB_TEXT | wxAUI_TB_HORZ_LAYOUT | wxAUI_TB_GRIPPER | wxAUI_TB_OVERFLOW);
        m_toolBar->AddTool(ID_SCALE_FIT, "Scale to Fit", wxNullBitmap,
                           "Scale the remote desktop to fit the window (otherwise show it at 100%)", wxITEM_CHECK);
        m_toolBar->AddTool(ID_FULL_SCREEN, "Full Screen", wxNullBitmap, "Full screen (F8 opens the menu)");
        m_toolBar->AddTool(ID_VIEW_ONLY, "View Only", wxNullBitmap, "Watch without sending mouse or keyboard input",
                           wxITEM_CHECK);
        m_toolBar->AddTool(ID_CLIPBOARD, "Clipboard", wxNullBitmap, "Share copied text with the remote computer",
                           wxITEM_CHECK);
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_SEND_CTRL_ALT_DEL, "Ctrl+Alt+Del", wxNullBitmap, "Send Ctrl+Alt+Del");
        // Marconi keeps F8, F9 and F10 for itself, so these send them to the remote computer.
        m_toolBar->AddTool(ID_SEND_F8, "F8", wxNullBitmap, "Send F8 to the remote computer");
        m_toolBar->AddTool(ID_SEND_F9, "F9", wxNullBitmap, "Send F9 to the remote computer");
        m_toolBar->AddTool(ID_SEND_F10, "F10", wxNullBitmap, "Send F10 to the remote computer");
        m_toolBar->AddTool(ID_REFRESH, "Refresh", wxNullBitmap, "Ask the server for a full screen update");
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_FILES, "Files", wxNullBitmap, "Copy files to and from the remote computer (F10)",
                           wxITEM_CHECK);
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_DISCONNECT, "Disconnect", wxNullBitmap, "Close this connection");
        m_toolBar->Realize();
        m_aui.AddPane(m_toolBar, wxAuiPaneInfo()
                                     .Name("Toolbar")
                                     .Caption("Toolbar")
                                     .ToolbarPane()
                                     .Top()
                                     .Floatable(true)
                                     .Dockable(true)
                                     .CloseButton(false));
    }

    wxMenu* ViewerFrame::CreateMenu() const
    {
        wxMenu* menu = new wxMenu();
        menu->AppendRadioItem(ID_SCALE_FIT, "Scale to &Fit");
        menu->AppendRadioItem(ID_SCALE_ACTUAL, "&Actual Size (100%)");
        menu->AppendCheckItem(ID_FULL_SCREEN, "F&ull Screen");
        menu->AppendCheckItem(ID_VIEW_ONLY, "&View Only");
        menu->AppendCheckItem(ID_CLIPBOARD, "Share C&lipboard");
        menu->AppendSeparator();
        menu->Append(ID_SEND_CTRL_ALT_DEL, "Send Ctrl+Alt+&Del");
        menu->Append(ID_SEND_F8, "Send F&8");
        menu->Append(ID_SEND_F9, "Send F&9");
        menu->Append(ID_SEND_F10, "Send F1&0");
        menu->Append(ID_REFRESH, "&Refresh Screen");
        menu->AppendSeparator();
        menu->AppendCheckItem(ID_SHOW_TOOLBAR, "Show &Toolbar\tF9");
        menu->AppendCheckItem(ID_FILES, "Transfer &Files\tF10");
        menu->AppendSeparator();
        menu->Append(ID_DISCONNECT, "Dis&connect");

        bool fit = m_canvas->GetScaleMode() == ScaleMode::Fit;
        menu->Check(fit ? ID_SCALE_FIT : ID_SCALE_ACTUAL, true);
        menu->Check(ID_FULL_SCREEN, IsFullScreen());
        menu->Check(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        menu->Check(ID_CLIPBOARD, m_clipboard.IsEnabled());
        menu->Check(ID_SHOW_TOOLBAR, m_toolBar->IsShown()); // AUI hides the toolbar window with its pane
        menu->Check(ID_FILES, m_files->IsShown());
        for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_F8, ID_SEND_F9, ID_SEND_F10, ID_REFRESH})
        {
            menu->Enable(id, m_connected && (id == ID_REFRESH || !m_canvas->IsViewOnly()));
        }
        return menu;
    }

    void ViewerFrame::ShowMenu()
    {
        std::unique_ptr<wxMenu> menu(CreateMenu());
        m_canvas->ReleaseInput();
        PopupMenu(menu.get(), m_canvas->GetPosition() + wxPoint(FromDIP(8), FromDIP(8)));
        m_canvas->SetFocus();
    }

    void ViewerFrame::OnCommand(wxCommandEvent& event)
    {
        // A toolbar click may have taken the keyboard focus; give it back to the desktop afterwards.
        CallAfter([this]() { m_canvas->SetFocus(); });
        switch (event.GetId())
        {
        case ID_SCALE_FIT:
            // The toolbar button toggles; the menu's radio item always selects Fit.
            m_canvas->SetScaleMode(event.GetEventType() == wxEVT_TOOL && !event.IsChecked() ? ScaleMode::Actual
                                                                                            : ScaleMode::Fit);
            break;
        case ID_SCALE_ACTUAL:
            m_canvas->SetScaleMode(ScaleMode::Actual);
            break;
        case ID_FULL_SCREEN:
            ToggleFullScreen();
            break;
        case ID_VIEW_ONLY:
            m_canvas->SetViewOnly(!m_canvas->IsViewOnly());
            break;
        case ID_CLIPBOARD:
            m_clipboard.SetEnabled(!m_clipboard.IsEnabled());
            if (m_clipboard.IsEnabled())
            {
                m_clipboard.Reset();
                SyncClipboardToServer();
            }
            break;
        case ID_SEND_CTRL_ALT_DEL:
            SendKeyCombination({Marconi::Keysym::Control_L, Marconi::Keysym::Alt_L, Marconi::Keysym::Delete});
            break;
        case ID_SEND_F8:
            SendKeyCombination({Marconi::Keysym::F1 + 7});
            break;
        case ID_SEND_F9:
            SendKeyCombination({Marconi::Keysym::F1 + 8});
            break;
        case ID_SEND_F10:
            SendKeyCombination({Marconi::Keysym::F1 + 9});
            break;
        case ID_SHOW_TOOLBAR:
            ToggleToolbar();
            break;
        case ID_FILES:
            ToggleFiles();
            break;
        case ID_REFRESH:
            m_client.RequestFullUpdate();
            break;
        case ID_DISCONNECT:
            Close();
            return;
        default:
            break;
        }
        UpdateControls();
        UpdateStatus();
        m_canvas->SetFocus();
        SyncClipboardToServer();
    }

    void ViewerFrame::SendKeyCombination(std::initializer_list<uint32_t> keysyms)
    {
        if (m_canvas->IsViewOnly())
        {
            return;
        }
        m_canvas->ReleaseInput();
        for (uint32_t keysym : keysyms)
        {
            m_client.SendKeyEvent(keysym, true);
        }
        for (auto it = std::rbegin(keysyms); it != std::rend(keysyms); ++it)
        {
            m_client.SendKeyEvent(*it, false);
        }
    }

    void ViewerFrame::ToggleFullScreen()
    {
        m_canvas->ReleaseInput();
        if (!IsFullScreen())
        {
            // Docked panels would cover part of the screen; floating ones stay where the user put them.
            m_layoutBeforeFullScreen = m_aui.SavePerspective();
            wxAuiPaneInfoArray& panes = m_aui.GetAllPanes();
            for (size_t i = 0; i < panes.GetCount(); ++i)
            {
                if (panes[i].name != "Screen" && !panes[i].IsFloating())
                {
                    panes[i].Hide();
                }
            }
            m_aui.Update();
            ShowFullScreen(true, wxFULLSCREEN_ALL);
            return;
        }
        ShowFullScreen(false, wxFULLSCREEN_ALL);
        m_aui.LoadPerspective(m_layoutBeforeFullScreen);
        FitToDesktop();
    }

    void ViewerFrame::ApplyLayout(const wxString& layout)
    {
        // A layout saved by another version may name panels this one doesn't have (ignored) or lack some it has.
        if (!m_aui.LoadPerspective(layout, false))
        {
            LOG_INFO("Viewer", "Couldn't restore the saved panel layout; using the default");
            m_aui.LoadPerspective(m_defaultLayout);
            return;
        }
        m_aui.GetPane("Screen").Show().CenterPane();
        // A panel floating on a monitor that's no longer connected comes back docked.
        wxAuiPaneInfoArray& panes = m_aui.GetAllPanes();
        for (size_t i = 0; i < panes.GetCount(); ++i)
        {
            if (panes[i].IsFloating() && wxDisplay::GetFromPoint(panes[i].floating_pos) == wxNOT_FOUND)
            {
                panes[i].Dock();
            }
        }
        m_aui.Update();
    }

    wxString ViewerFrame::GetLayout() const
    {
        if (IsFullScreen())
        {
            return m_layoutBeforeFullScreen;
        }
        return const_cast<wxAuiManager&>(m_aui).SavePerspective(); // SavePerspective isn't const, but doesn't change it
    }

    void ViewerFrame::UpdateControls()
    {
        m_toolBar->ToggleTool(ID_SCALE_FIT, m_canvas->GetScaleMode() == ScaleMode::Fit);
        m_toolBar->ToggleTool(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        m_toolBar->ToggleTool(ID_CLIPBOARD, m_clipboard.IsEnabled());
        for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_F8, ID_SEND_F9, ID_SEND_F10})
        {
            m_toolBar->EnableTool(id, m_connected && !m_canvas->IsViewOnly());
        }
        m_toolBar->EnableTool(ID_REFRESH, m_connected);
        m_toolBar->ToggleTool(ID_FILES, m_files != nullptr && m_files->IsShown());
        m_toolBar->Refresh();
    }

    void ViewerFrame::ToggleFiles()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Files");
        if (pane.IsShown())
        {
            pane.Hide();
            m_aui.Update();
            m_canvas->SetFocus();
        }
        else
        {
            ShowFiles();
        }
        UpdateControls();
    }

    void ViewerFrame::ShowFiles()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Files");
        if (!pane.IsShown())
        {
            pane.Show();
            m_aui.Update();
        }
        m_files->Activate();
    }

    void ViewerFrame::ToggleToolbar()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Toolbar");
        pane.Show(!pane.IsShown());
        m_aui.Update();
    }

    void ViewerFrame::UpdateStatus()
    {
        if (!m_connected)
        {
            return;
        }
        wxSize desktop = m_canvas->GetDesktopSize();
        wxString status = wxString::Format("Connected to %s (%d x %d)", GetServerDisplayName(), desktop.x, desktop.y);
        // Always say whether the connection is encrypted: without encryption, the picture and every key typed can
        // be read by anyone on the network between the two computers.
        status += m_client.IsEncrypted() ? " - encrypted, " + wxString::FromUTF8(m_client.GetEncryptionName())
                                         : wxString(" - not encrypted");
        if (m_waitingForPicture)
        {
            status += " - waiting for the screen...";
        }
        if (m_canvas->GetScaleMode() == ScaleMode::Fit)
        {
            status += wxString::Format(" - %d%%", static_cast<int>(m_canvas->GetScale() * 100.0 + 0.5));
        }
        if (m_canvas->IsViewOnly())
        {
            status += " - view only";
        }
        SetStatusText(status);
    }

    void ViewerFrame::SaveSettings()
    {
        // Reopening a saved connection restores how this window was left.
        ConnectionStore store;
        SavedConnection saved;
        if (m_options.savedId.empty() || !store.Load(m_options.savedId, saved))
        {
            return;
        }
        saved.scaleMode = m_canvas->GetScaleMode();
        saved.viewOnly = m_canvas->IsViewOnly();
        saved.clipboard = m_clipboard.IsEnabled();
        saved.layout = GetLayout();
        store.Save(saved);
    }

    void ViewerFrame::OnClose(wxCloseEvent& event)
    {
        if (m_files != nullptr && m_files->IsBusy() && event.CanVeto() && !m_options.headless &&
            wxMessageBox("Files are still being copied. Stop copying and close?", "Marconi",
                         wxYES_NO | wxICON_QUESTION, this) != wxYES)
        {
            event.Veto();
            return;
        }
        if (m_files != nullptr)
        {
            m_files->Shutdown();
        }
        if (!m_options.headless)
        {
            SaveSettings(); // a headless run must not change the user's saved settings
        }
        {
            std::lock_guard<std::mutex> lock(m_questionMutex);
            m_closing = true;
        }
        m_questionChanged.notify_all();
        StopProbe();
        StopTimers();
        m_canvas->ReleaseInput();
        m_client.Disconnect();
        event.Skip(); // default handler destroys the frame
        if (m_options.headless)
        {
            // A hidden frame isn't always deleted promptly (pending deletes wait for idle time), so end the app.
            wxTheApp->ExitMainLoop();
        }
    }
} // namespace MarconiUI
