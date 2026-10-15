// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#include "ViewerFrame.h"

#include "CredentialsDialog.h"
#include "VncCanvas.h"

#include <Log/Log.h>

#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <algorithm>

namespace RmVncUI
{
    ViewerFrame::ViewerFrame(const ViewerOptions& options)
        : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(RmVnc::FormatAddress(options.address)) + " - rmvnc"),
          m_options(options),
          m_client(*this)
    {
        m_canvas = new VncCanvas(this, m_client);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_canvas, 1, wxEXPAND);
        SetSizer(sizer);

        CreateStatusBar();
        SetClientSize(FromDIP(wxSize(800, 600)));

        Bind(wxEVT_CLOSE_WINDOW, &ViewerFrame::OnClose, this);
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
        SetStatusText(wxString::Format("Connected to %s (%d x %d)", GetServerDisplayName(), serverInit.width,
                                       serverInit.height));
        m_canvas->SetDesktopSize(serverInit.width, serverInit.height);
        FitToDesktop();
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
