// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread.
// The rest of the window is in ViewerFrameConnection.cpp (connecting and logging in), ViewerFrameRecovery.cpp
// (stalled pictures and lost connections), ViewerFrameMenus.cpp (toolbar, menu and commands) and
// ViewerFrameCapture.cpp (screenshots and the clipboard).

#include "ViewerFrame.h"

#include "AppIcon.h"
#include "ConnectionStore.h"
#include "FilePanel.h"
#include "ViewerCommands.h"
#include "VncCanvas.h"

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <wx/app.h>
#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace MarconiUI
{
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
        m_aui.SetDockSizeConstraint(0.5, 0.6); // the file panel needs room: up to 60% of the height (wx: 30%)
        m_aui.AddPane(m_canvas, wxAuiPaneInfo().Name("Screen").CenterPane());

        CreateToolbar();
        CreateStatusBar(2); // the connection on the left; the round trip to the server on the right
        const int statusWidths[] = {-1, FromDIP(130)};
        SetStatusWidths(2, statusWidths);
        m_lostBar = new ConnectionLostBar(this, [this]() { RetryNow(); }, [this]() { Close(); });
        // Not managed by AUI: it floats over everything at the top of the screen while in full screen.
        m_fullScreenBar = new FullScreenBar(
            this, wxString::FromUTF8(m_options.address.host),
            {{"Fit", ID_SCALE_FIT, "Scale the remote desktop to fit the screen"},
             {"Zoom In", ID_ZOOM_IN, "Zoom in"},
             {"Zoom Out", ID_ZOOM_OUT, "Zoom out"},
             {"Screenshot", ID_SCREENSHOT_SAVE, "Save a picture of the remote desktop"},
             {"Send Keys", ID_SEND_KEYS, "Send Alt+Tab, the Windows key and other shortcuts"},
             {"Files", ID_FILES, "Copy files to and from the remote computer"},
             {"Menu", ID_SHOW_MENU, "Marconi's menu (F8)"},
             {"Exit Full Screen", ID_FULL_SCREEN, "Back to a window"}});
        m_aui.AddPane(m_lostBar, wxAuiPaneInfo()
                                     .Name("ConnectionLost")
                                     .Top()
                                     .Layer(3)
                                     .CaptionVisible(false)
                                     .CloseButton(false)
                                     .Floatable(false)
                                     .Movable(false)
                                     .Fixed()
                                     .Hide());
        FilePanel::Remote remote;
        remote.host = m_options.address.host;
        remote.username = m_options.credentials.username;
        remote.password = m_options.credentials.password;
        remote.displayName = wxString::FromUTF8(m_options.address.host);
        remote.savedId = m_options.savedId;
        remote.trustedKey = m_options.sshHostKey;
        remote.dropFolder = m_options.dropFolder;
        m_files = new FilePanel(this, remote);
        m_aui.AddPane(m_files, wxAuiPaneInfo()
                                   .Name("Files")
                                   .Caption("Files")
                                   .Bottom()
                                   .BestSize(FromDIP(wxSize(1000, 480)))
                                   .MinSize(FromDIP(wxSize(480, 420)))
                                   .Floatable(true)
                                   .Dockable(true)
                                   .Hide());
        m_canvas->SetFileDropHandler([this](const std::vector<std::string>& paths) { UploadToRemoteDesktop(paths); });
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
        if (!options.windowRect.IsEmpty() && wxDisplay::GetFromPoint(options.windowRect.GetPosition()) != wxNOT_FOUND &&
            !options.headless)
        {
            SetSize(options.windowRect); // back where it was for this computer
            m_windowRestored = true;
        }
        m_quality = options.quality;

        m_canvas->SetLocalCommandHandler([this](Marconi::LocalCommand command) {
            switch (command)
            {
            case Marconi::LocalCommand::Menu:
                ShowMenu();
                break;
            case Marconi::LocalCommand::Toolbar:
                ToggleToolbar();
                break;
            case Marconi::LocalCommand::Files:
                ToggleFiles();
                break;
            case Marconi::LocalCommand::ZoomIn:
            case Marconi::LocalCommand::ZoomOut:
                m_canvas->ZoomStep(command == Marconi::LocalCommand::ZoomIn,
                                   m_canvas->ScreenToClient(wxGetMousePosition()));
                UpdateControls();
                UpdateStatus();
                break;
            case Marconi::LocalCommand::ZoomReset:
                m_canvas->SetZoom(ScaleMode::Fixed, 100);
                UpdateControls();
                UpdateStatus();
                break;
            case Marconi::LocalCommand::PasteFiles:
                PasteFilesFromClipboard();
                break;
            default:
                break;
            }
        });
        m_canvas->SetZoom(options.scaleMode, options.zoomPercent);
        m_canvas->SetViewOnly(options.viewOnly);
        m_clipboard.SetEnabled(options.clipboard);
        m_clipboard.SetStatusHandler([this](const wxString& message) { SetStatusText(message); });
        m_sendSystemKeys = options.systemKeys && SystemKeyCapture::IsSupported();
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            event.Skip();
            m_active = event.GetActive();
            UpdateSystemKeyCapture();
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
        m_healthTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnHealthTick(); }, m_healthTimer.GetId());
        m_clipboardTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { WatchClipboard(); }, m_clipboardTimer.GetId());
        m_retryTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnRetryTick(); }, m_retryTimer.GetId());
        m_matchSizeTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RequestMatchingSize(); }, m_matchSizeTimer.GetId());
        m_displayId = options.displayId;
        Bind(wxEVT_MENU, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_TOOL, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            CallAfter([this]() { UpdateStatus(); }); // after the canvas has recomputed its scale
            if (m_matchWindowSize && m_connected)
            {
                m_matchSizeTimer.StartOnce(500); // once the resize has settled
            }
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
        options.zoomPercent = connection.zoomPercent;
        options.displayId = connection.displayId;
        options.quality = Marconi::QualityFromName(connection.quality.utf8_string());
        options.lockOnDisconnect = connection.lockOnDisconnect;
        options.macAddress = connection.macAddress;
        options.windowRect = connection.windowRect;
        options.viewOnly = connection.viewOnly;
        options.clipboard = connection.clipboard;
        options.systemKeys = connection.systemKeys;
        options.layout = connection.layout;
        options.sshHostKey = connection.sshHostKey;
        options.dropFolder = connection.dropFolder;
        out = options;
        return true;
    }

    void ViewerFrame::StopTimers()
    {
        StopConnectionTimers();
        m_headlessSnapshotTimer.Stop();
        m_quitTimer.Stop();
    }

    void ViewerFrame::StopConnectionTimers()
    {
        m_pictureTimer.Stop();
        m_statsTimer.Stop();
        m_clipboardTimer.Stop();
        m_healthTimer.Stop();
        SetStatusText(wxEmptyString, 1); // no connection, no round trip
    }

    ViewerFrame::~ViewerFrame()
    {
        m_aui.UnInit(); // before the panes it manages are destroyed
        if (m_tightFiles)
        {
            m_tightFiles->Close();
        }
        if (m_files != nullptr)
        {
            m_files->Shutdown(); // its copies use m_tightFiles and the client, which go before it does
        }
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

    void ViewerFrame::OnScreenLayout(const std::vector<Marconi::Rfb::Screen>& screens)
    {
        CallAfter([this, screens]() {
            m_screens = screens;
            ApplyDisplayChoice();
        });
    }

    void ViewerFrame::OnDesktopResizeRefused(int status)
    {
        CallAfter([this, status]() {
            SetStatusText(status == 1 ? "The other computer doesn't allow its screen size to be changed"
                                      : "The other computer couldn't change its screen size");
        });
    }

    void ViewerFrame::ApplyDisplayChoice()
    {
        // Show the chosen display when the server has one with that id; otherwise everything.
        wxRect viewport;
        for (const Marconi::Rfb::Screen& screen : m_screens)
        {
            if (m_displayId != 0 && static_cast<long>(screen.id) == m_displayId && m_screens.size() > 1)
            {
                viewport = wxRect(screen.x, screen.y, screen.width, screen.height);
            }
        }
        if (viewport != m_canvas->GetViewport())
        {
            m_canvas->SetViewport(viewport);
        }
        UpdateStatus();
    }

    void ViewerFrame::RequestMatchingSize()
    {
        if (!m_matchWindowSize || !m_connected || !m_client.SupportsDesktopResize())
        {
            return;
        }
        wxSize client = m_canvas->GetClientSize();
        if (m_canvas->GetScaleMode() == ScaleMode::Fixed)
        {
            client.x = static_cast<int>(std::lround(client.x * 100.0 / m_canvas->GetZoomPercent()));
            client.y = static_cast<int>(std::lround(client.y * 100.0 / m_canvas->GetZoomPercent()));
        }
        if (client.x < 64 || client.y < 64 || client == m_canvas->GetDesktopSize())
        {
            return;
        }
        m_client.RequestDesktopSize(client.x, client.y);
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

    void ViewerFrame::OnServerClipboard(const Marconi::Rfb::ClipboardContent& content)
    {
        CallAfter([this, content]() { m_clipboard.OnServerContent(content); });
    }

    void ViewerFrame::OnBell()
    {
        CallAfter([]() { wxBell(); });
    }

    void ViewerFrame::OnDisconnected(const Marconi::Rfb::DisconnectInfo& info)
    {
        CallAfter([this, info]() { HandleDisconnected(info); });
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
        if (m_canvas->GetScaleMode() == ScaleMode::Fixed)
        {
            desktop.x = static_cast<int>(std::lround(desktop.x * m_canvas->GetZoomPercent() / 100.0));
            desktop.y = static_cast<int>(std::lround(desktop.y * m_canvas->GetZoomPercent() / 100.0));
        }
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
        m_aui.GetPane("ConnectionLost").Hide(); // only ever shown while a connection is being retried
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
        saved.zoomPercent = m_canvas->GetZoomPercent();
        saved.displayId = m_displayId;
        saved.quality = Marconi::QualityName(m_quality);
        if (!IsFullScreen() && !IsMaximized() && !IsIconized())
        {
            saved.windowRect = GetRect();
        }
        saved.viewOnly = m_canvas->IsViewOnly();
        saved.clipboard = m_clipboard.IsEnabled();
        saved.systemKeys = m_sendSystemKeys;
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
        if (m_tightFiles)
        {
            m_tightFiles->Close();
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
        m_retryTimer.Stop();
        if (m_systemKeys)
        {
            m_systemKeys->Stop();
        }
        m_canvas->ReleaseInput();
        if (m_options.lockOnDisconnect && m_connected && !m_canvas->IsViewOnly())
        {
            // Win+L on Windows and most Linux desktops; Ctrl+Cmd+Q on a Mac (Command is Super over VNC).
            LOG_INFO("Viewer", "Locking the other computer's screen before disconnecting");
            if (m_client.IsAppleServer())
            {
                SendKeyCombination({Marconi::Keysym::Control_L, Marconi::Keysym::Super_L, 'q'});
            }
            else
            {
                SendKeyCombination({Marconi::Keysym::Super_L, 'l'});
            }
        }
        m_client.Disconnect();
        event.Skip(); // default handler destroys the frame
        if (m_options.headless)
        {
            // A hidden frame isn't always deleted promptly (pending deletes wait for idle time), so end the app.
            wxTheApp->ExitMainLoop();
        }
    }
} // namespace MarconiUI
