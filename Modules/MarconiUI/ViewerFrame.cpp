// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#include "ViewerFrame.h"

#include "AppIcon.h"
#include "ClipboardWatch.h"
#include "ConnectionStore.h"
#include "CredentialsDialog.h"
#include "VncCanvas.h"

#include <MarconiCore/Keysyms.h>

#include <Log/Log.h>

#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

#include <algorithm>

namespace MarconiUI
{
    namespace
    {
        enum CommandId
        {
            ID_SCALE_FIT = wxID_HIGHEST + 100,
            ID_SCALE_ACTUAL,
            ID_FULL_SCREEN,
            ID_VIEW_ONLY,
            ID_CLIPBOARD,
            ID_SEND_CTRL_ALT_DEL,
            ID_SEND_F8,
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
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_canvas, 1, wxEXPAND);
        SetSizer(sizer);

        CreateToolbar();
        CreateStatusBar();
        SetClientSize(FromDIP(wxSize(800, 600)));

        m_canvas->SetMenuKeyHandler([this]() { ShowMenu(); });
        m_canvas->SetScaleMode(options.scaleMode);
        m_canvas->SetViewOnly(options.viewOnly);
        m_clipboard.SetEnabled(options.clipboard);
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            event.Skip();
            if (event.GetActive())
            {
                // Someone who copied something locally is probably about to paste it remotely.
                CallAfter([this]() { SyncClipboardToServer(); });
            }
        });
        Bind(wxEVT_CLOSE_WINDOW, &ViewerFrame::OnClose, this);
        m_pictureTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { CheckForPicture(); }, m_pictureTimer.GetId());
        m_statsTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { LogTraffic(); }, m_statsTimer.GetId());
        m_snapshotTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { SaveSnapshot(); }, m_snapshotTimer.GetId());
        m_clipboardTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { WatchClipboard(); }, m_clipboardTimer.GetId());
        Bind(wxEVT_MENU, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_TOOL, &ViewerFrame::OnCommand, this, ID_SCALE_FIT, ID_DISCONNECT);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            CallAfter([this]() { UpdateStatus(); }); // after the canvas has recomputed its scale
        });
        UpdateControls();
        Connect();
    }

    ViewerFrame::~ViewerFrame()
    {
        // Normally already done in OnClose; the Client must stop before this listener goes away.
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            m_closing = true;
        }
        m_credentialsChanged.notify_all();
        m_client.Disconnect();
    }

    wxString ViewerFrame::GetServerDisplayName() const
    {
        return m_desktopName.empty() ? wxString::FromUTF8(Marconi::FormatAddress(m_options.address)) : m_desktopName;
    }

    void ViewerFrame::Connect()
    {
        m_clipboard.Reset();
        SetStatusText("Connecting to " + wxString::FromUTF8(Marconi::FormatAddress(m_options.address)) + "...");
        Marconi::Rfb::ConnectionOptions connection;
        connection.host = m_options.address.host;
        connection.port = m_options.address.port;
        connection.credentials = m_options.credentials;
        m_client.Connect(connection);
    }

    void ViewerFrame::OnConnected(const Marconi::Rfb::ServerInit& serverInit)
    {
        CallAfter([this, serverInit]() { HandleConnected(serverInit); });
    }

    void ViewerFrame::OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects)
    {
        CallAfter([this, rects]() { m_canvas->UpdateRects(rects); });
    }

    void ViewerFrame::OnDesktopResized(int width, int height)
    {
        CallAfter([this, width, height]() {
            // The window keeps its size (Fit mode scales): a Mac switches size whenever it locks or unlocks, and
            // the window jumping around with it is disorienting. It was fitted to the desktop on connecting.
            m_canvas->SetDesktopSize(width, height);
            UpdateStatus();
            m_snapshotTimer.StartOnce(3000);
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
        if (m_connected && !m_canvas->IsViewOnly())
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

    bool ViewerFrame::OnCredentialsRequired(Marconi::Rfb::CredentialsKind kind, Marconi::Rfb::Credentials& credentials)
    {
        // Connection thread: ask the UI thread and wait for the answer (or for the window to close).
        std::unique_lock<std::mutex> lock(m_credentialsMutex);
        if (m_closing)
        {
            return false;
        }
        m_credentialsAnswered = false;
        CallAfter([this, kind]() { AskForCredentials(kind); });
        m_credentialsChanged.wait(lock, [this]() { return m_credentialsAnswered || m_closing; });
        if (m_closing || !m_credentialsAccepted)
        {
            return false;
        }
        credentials = m_credentialsAnswer;
        return true;
    }

    void ViewerFrame::AskForCredentials(Marconi::Rfb::CredentialsKind kind)
    {
        {
            // The window may have started closing after this was queued; nobody is waiting any more.
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            if (m_closing)
            {
                return;
            }
        }
        SetStatusText("Waiting for credentials...");
        bool offerRemember = !m_options.savedId.empty() && ConnectionStore::IsKeychainAvailable();
        CredentialsDialog dialog(this, GetServerDisplayName(), kind,
                                 wxString::FromUTF8(m_options.credentials.username), offerRemember);
        bool accepted = dialog.ShowModal() == wxID_OK;
        m_rememberPassword = accepted && dialog.GetRemember();
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            m_credentialsAccepted = accepted;
            m_credentialsAnswer.username = dialog.GetUsername().utf8_string();
            m_credentialsAnswer.password = dialog.GetPassword().utf8_string();
            m_credentialsAnswered = true;
        }
        m_credentialsChanged.notify_all();
        if (accepted)
        {
            // Keep them for the rest of this window's life so reconnects don't ask again.
            m_options.credentials = m_credentialsAnswer;
            SetStatusText("Authenticating...");
        }
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
            ShowFullScreen(true, wxFULLSCREEN_ALL);
        }
        UpdateControls();
        UpdateStatus();
        m_canvas->SetFocus();
        SyncClipboardToServer();
        m_pictureRetries = 0;
        m_pictureTimer.Start(2000);
        m_client.TakeTrafficStats(); // start counting from here
        m_statsTimer.Start(10000);
        m_clipboardChangeCount = GetClipboardChangeCount();
        m_clipboardTimer.Start(500);
        m_snapshotTimer.StartOnce(3000);
    }

    void ViewerFrame::WatchClipboard()
    {
        // Copying while the viewer is in front doesn't change focus, so watch for changes rather than waiting
        // for the window to be activated again.
        long count = GetClipboardChangeCount();
        if (count >= 0 && count != m_clipboardChangeCount)
        {
            m_clipboardChangeCount = count;
            SyncClipboardToServer();
        }
    }

    void ViewerFrame::SaveSnapshot()
    {
        // Diagnostics: what the framebuffer holds after connecting and after a resize, saved next to the logs.
        constexpr int kMaxSnapshots = 10;
        if (!m_connected || m_snapshots >= kMaxSnapshots)
        {
            return;
        }
        wxImage image;
        {
            Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            if (framebuffer.GetWidth() <= 0 || framebuffer.GetHeight() <= 0)
            {
                return;
            }
            image.Create(framebuffer.GetWidth(), framebuffer.GetHeight(), false);
            unsigned char* out = image.GetData();
            for (int y = 0; y < framebuffer.GetHeight(); ++y)
            {
                const uint32_t* row = framebuffer.Row(y);
                for (int x = 0; x < framebuffer.GetWidth(); ++x)
                {
                    *out++ = static_cast<unsigned char>(row[x] >> 16);
                    *out++ = static_cast<unsigned char>(row[x] >> 8);
                    *out++ = static_cast<unsigned char>(row[x]);
                }
            }
        }
        ++m_snapshots;
        wxString path = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs" + wxFILE_SEP_PATH +
                        wxDateTime::Now().Format("snapshot_%Y-%m-%d_%H-%M-%S_") +
                        wxString::Format("%dx%d.png", image.GetWidth(), image.GetHeight());
        if (image.SaveFile(path, wxBITMAP_TYPE_PNG))
        {
            LOG_INFO("Viewer", "Saved a snapshot of the desktop to %s", path.utf8_string().c_str());
        }
    }

    void ViewerFrame::LogTraffic()
    {
        // Shows whether the server keeps sending pictures, e.g. when the view freezes.
        Marconi::Rfb::TrafficStats stats = m_client.TakeTrafficStats();
        LOG_INFO("Viewer", "Last 10s: %d updates with %d rectangles, %d requests, %d pointer and %d key events sent",
                 stats.updates, stats.rectangles, stats.requests, stats.pointerEvents, stats.keyEvents);
    }

    void ViewerFrame::CheckForPicture()
    {
        // macOS sometimes answers a connection made soon after the previous one with an empty or all-black desktop
        // and then reports no changes. Asking for the whole desktop again brings the picture back.
        constexpr int kMaxRetries = 5;
        bool black;
        {
            Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            black = framebuffer.IsBlack();
        }
        if (!m_connected || !black || m_pictureRetries >= kMaxRetries)
        {
            m_pictureTimer.Stop();
            return;
        }
        ++m_pictureRetries;
        LOG_INFO("Viewer", "The desktop is still black, asking for it again (%d/%d)", m_pictureRetries, kMaxRetries);
        m_client.RequestFullUpdate();
    }

    void ViewerFrame::HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info)
    {
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            if (m_closing)
            {
                return;
            }
        }
        LOG_INFO("Viewer", "Disconnected: %s", info.error.empty() ? "(no error)" : info.error.c_str());
        m_pictureTimer.Stop();
        m_statsTimer.Stop();
        m_snapshotTimer.Stop();
        m_clipboardTimer.Stop();
        m_canvas->ReleaseInput();
        m_connected = false;
        UpdateControls();
        if (info.error.empty())
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
                Connect();
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
            Connect();
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
        wxSize decorations = GetSize() - GetClientSize(); // includes the status bar
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
        m_toolBar = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NOICONS | wxTB_FLAT);
        m_toolBar->AddCheckTool(ID_SCALE_FIT, "Scale to Fit", wxNullBitmap, wxNullBitmap,
                                "Scale the remote desktop to fit the window (otherwise show it at 100%)");
        m_toolBar->AddTool(ID_FULL_SCREEN, "Full Screen", wxNullBitmap, "Full screen (F8 opens the menu)");
        m_toolBar->AddCheckTool(ID_VIEW_ONLY, "View Only", wxNullBitmap, wxNullBitmap,
                                "Watch without sending mouse or keyboard input");
        m_toolBar->AddCheckTool(ID_CLIPBOARD, "Clipboard", wxNullBitmap, wxNullBitmap,
                                "Share copied text with the remote computer");
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_SEND_CTRL_ALT_DEL, "Ctrl+Alt+Del", wxNullBitmap, "Send Ctrl+Alt+Del");
        m_toolBar->AddTool(ID_REFRESH, "Refresh", wxNullBitmap, "Ask the server for a full screen update");
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_DISCONNECT, "Disconnect", wxNullBitmap, "Close this connection");
        m_toolBar->Realize();
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
        menu->Append(ID_REFRESH, "&Refresh Screen");
        menu->AppendSeparator();
        menu->Append(ID_DISCONNECT, "Dis&connect");

        bool fit = m_canvas->GetScaleMode() == ScaleMode::Fit;
        menu->Check(fit ? ID_SCALE_FIT : ID_SCALE_ACTUAL, true);
        menu->Check(ID_FULL_SCREEN, IsFullScreen());
        menu->Check(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        menu->Check(ID_CLIPBOARD, m_clipboard.IsEnabled());
        for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_F8, ID_REFRESH})
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
        ShowFullScreen(!IsFullScreen(), wxFULLSCREEN_ALL);
        if (!IsFullScreen())
        {
            FitToDesktop();
        }
    }

    void ViewerFrame::UpdateControls()
    {
        m_toolBar->ToggleTool(ID_SCALE_FIT, m_canvas->GetScaleMode() == ScaleMode::Fit);
        m_toolBar->ToggleTool(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        m_toolBar->ToggleTool(ID_CLIPBOARD, m_clipboard.IsEnabled());
        m_toolBar->EnableTool(ID_SEND_CTRL_ALT_DEL, m_connected && !m_canvas->IsViewOnly());
        m_toolBar->EnableTool(ID_REFRESH, m_connected);
    }

    void ViewerFrame::UpdateStatus()
    {
        if (!m_connected)
        {
            return;
        }
        wxSize desktop = m_canvas->GetDesktopSize();
        wxString status = wxString::Format("Connected to %s (%d x %d)", GetServerDisplayName(), desktop.x, desktop.y);
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
        store.Save(saved);
    }

    void ViewerFrame::OnClose(wxCloseEvent& event)
    {
        SaveSettings();
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            m_closing = true;
        }
        m_credentialsChanged.notify_all();
        m_canvas->ReleaseInput();
        m_client.Disconnect();
        event.Skip(); // default handler destroys the frame
    }
} // namespace MarconiUI
