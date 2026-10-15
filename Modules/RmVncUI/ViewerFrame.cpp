// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#include "ViewerFrame.h"

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
        SetStatusText("Connecting to " + wxString::FromUTF8(RmVnc::FormatAddress(options.address)) + "...");
        SetClientSize(FromDIP(wxSize(800, 600)));

        Bind(wxEVT_CLOSE_WINDOW, &ViewerFrame::OnClose, this);

        RmVnc::Rfb::ConnectionOptions connection;
        connection.host = options.address.host;
        connection.port = options.address.port;
        connection.credentials = options.credentials;
        m_client.Connect(connection);
    }

    ViewerFrame::~ViewerFrame()
    {
        // Normally already done in OnClose; the Client must stop before this listener goes away.
        m_closing = true;
        m_client.Disconnect();
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

    void ViewerFrame::OnBell()
    {
        CallAfter([]() { wxBell(); });
    }

    void ViewerFrame::OnDisconnected(const std::string& error)
    {
        CallAfter([this, error]() { HandleDisconnected(error); });
    }

    void ViewerFrame::HandleConnected(const RmVnc::Rfb::ServerInit& serverInit)
    {
        wxString name = wxString::FromUTF8(serverInit.name);
        if (name.empty())
        {
            name = wxString::FromUTF8(RmVnc::FormatAddress(m_options.address));
        }
        SetTitle(name + " - rmvnc");
        SetStatusText(wxString::Format("Connected to %s (%d x %d)", name, serverInit.width, serverInit.height));
        m_canvas->SetDesktopSize(serverInit.width, serverInit.height);
        FitToDesktop();
        m_canvas->SetFocus();
    }

    void ViewerFrame::HandleDisconnected(const std::string& error)
    {
        LOG_INFO("Viewer", "Disconnected (closing=%d): %s", m_closing ? 1 : 0, error.c_str());
        if (m_closing)
        {
            return;
        }
        if (error.empty())
        {
            Close(true);
            return;
        }
        SetStatusText("Disconnected: " + wxString::FromUTF8(error));
        wxMessageBox(wxString::FromUTF8(error), "Connection to " + wxString::FromUTF8(RmVnc::FormatAddress(m_options.address)),
                     wxOK | wxICON_ERROR, this);
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
        m_closing = true;
        m_client.Disconnect();
        event.Skip(); // default handler destroys the frame
    }
} // namespace RmVncUI
