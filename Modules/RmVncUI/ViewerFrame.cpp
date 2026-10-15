// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#include "ViewerFrame.h"

#include "CredentialsDialog.h"
#include "VncCanvas.h"

#include <RmVncCore/Keysyms.h>

#include <Log/Log.h>

#include <wx/display.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

#include <algorithm>

namespace RmVncUI
{
    namespace
    {
        enum CommandId
        {
            ID_SCALE_FIT = wxID_HIGHEST + 100,
            ID_SCALE_ACTUAL,
            ID_FULL_SCREEN,
            ID_VIEW_ONLY,
            ID_SEND_CTRL_ALT_DEL,
            ID_SEND_F8,
            ID_REFRESH,
            ID_DISCONNECT,
        };
    } // namespace

    ViewerFrame::ViewerFrame(const ViewerOptions& options)
        : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(RmVnc::FormatAddress(options.address)) + " - rmvnc"),
          m_options(options),
          m_client(*this)
    {
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
        Bind(wxEVT_CLOSE_WINDOW, &ViewerFrame::OnClose, this);
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
        return m_desktopName.empty() ? wxString::FromUTF8(RmVnc::FormatAddress(m_options.address)) : m_desktopName;
    }

    void ViewerFrame::Connect()
    {
        SetStatusText("Connecting to " + wxString::FromUTF8(RmVnc::FormatAddress(m_options.address)) + "...");
        RmVnc::Rfb::ConnectionOptions connection;
        connection.host = m_options.address.host;
        connection.port = m_options.address.port;
        connection.credentials = m_options.credentials;
        m_client.Connect(connection);
    }

    void ViewerFrame::OnConnected(const RmVnc::Rfb::ServerInit& serverInit)
    {
        CallAfter([this, serverInit]() { HandleConnected(serverInit); });
    }

    void ViewerFrame::OnFramebufferUpdated(const std::vector<RmVnc::Rfb::Rect>& rects)
    {
        CallAfter([this, rects]() { m_canvas->UpdateRects(rects); });
    }

    void ViewerFrame::OnDesktopResized(int width, int height)
    {
        CallAfter([this, width, height]() {
            m_canvas->SetDesktopSize(width, height);
            FitToDesktop();
            UpdateStatus();
        });
    }

    void ViewerFrame::OnCursorShape(const RmVnc::Rfb::CursorShape& cursor)
    {
        CallAfter([this, cursor]() { m_canvas->SetRemoteCursor(cursor); });
    }

    void ViewerFrame::OnBell()
    {
        CallAfter([]() { wxBell(); });
    }

    void ViewerFrame::OnDisconnected(const RmVnc::Rfb::DisconnectInfo& info)
    {
        CallAfter([this, info]() { HandleDisconnected(info); });
    }

    bool ViewerFrame::OnCredentialsRequired(bool needUsername, RmVnc::Rfb::Credentials& credentials)
    {
        // Connection thread: ask the UI thread and wait for the answer (or for the window to close).
        std::unique_lock<std::mutex> lock(m_credentialsMutex);
        if (m_closing)
        {
            return false;
        }
        m_credentialsAnswered = false;
        CallAfter([this, needUsername]() { AskForCredentials(needUsername); });
        m_credentialsChanged.wait(lock, [this]() { return m_credentialsAnswered || m_closing; });
        if (m_closing || !m_credentialsAccepted)
        {
            return false;
        }
        credentials = m_credentialsAnswer;
        return true;
    }

    void ViewerFrame::AskForCredentials(bool needUsername)
    {
        SetStatusText("Waiting for credentials...");
        CredentialsDialog dialog(this, GetServerDisplayName(), needUsername,
                                 wxString::FromUTF8(m_options.credentials.username));
        bool accepted = dialog.ShowModal() == wxID_OK;
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

    void ViewerFrame::HandleConnected(const RmVnc::Rfb::ServerInit& serverInit)
    {
        m_desktopName = wxString::FromUTF8(serverInit.name);
        SetTitle(GetServerDisplayName() + " - rmvnc");
        m_connected = true;
        m_canvas->SetDesktopSize(serverInit.width, serverInit.height);
        FitToDesktop();
        if (m_options.fullScreen && !IsFullScreen())
        {
            ShowFullScreen(true, wxFULLSCREEN_ALL);
        }
        UpdateControls();
        UpdateStatus();
        m_canvas->SetFocus();
    }

    void ViewerFrame::HandleDisconnected(const RmVnc::Rfb::DisconnectInfo& info)
    {
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            if (m_closing)
            {
                return;
            }
        }
        LOG_INFO("Viewer", "Disconnected: %s", info.error.empty() ? "(no error)" : info.error.c_str());
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
        wxMessageBox(wxString::FromUTF8(info.error), caption, wxOK | wxICON_ERROR, this);
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
        case ID_SEND_CTRL_ALT_DEL:
            SendKeyCombination({RmVnc::Keysym::Control_L, RmVnc::Keysym::Alt_L, RmVnc::Keysym::Delete});
            break;
        case ID_SEND_F8:
            SendKeyCombination({RmVnc::Keysym::F1 + 7});
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

    void ViewerFrame::OnClose(wxCloseEvent& event)
    {
        {
            std::lock_guard<std::mutex> lock(m_credentialsMutex);
            m_closing = true;
        }
        m_credentialsChanged.notify_all();
        m_canvas->ReleaseInput();
        m_client.Disconnect();
        event.Skip(); // default handler destroys the frame
    }
} // namespace RmVncUI
