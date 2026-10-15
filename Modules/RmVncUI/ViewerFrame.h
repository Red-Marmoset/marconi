// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include <RmVncCore/Address.h>
#include <RmVncCore/Rfb/Client.h>

#include <wx/frame.h>

namespace RmVncUI
{
    class VncCanvas;

    struct ViewerOptions
    {
        RmVnc::Address address;
        RmVnc::Rfb::Credentials credentials;
    };

    class ViewerFrame : public wxFrame, private RmVnc::Rfb::ClientListener
    {
    public:
        explicit ViewerFrame(const ViewerOptions& options);
        ~ViewerFrame() override;

    private:
        // ClientListener (connection thread)
        void OnConnected(const RmVnc::Rfb::ServerInit& serverInit) override;
        void OnFramebufferUpdated(const std::vector<RmVnc::Rfb::Rect>& rects) override;
        void OnDesktopResized(int width, int height) override;
        void OnBell() override;
        void OnDisconnected(const std::string& error) override;

        // UI thread
        void HandleConnected(const RmVnc::Rfb::ServerInit& serverInit);
        void HandleDisconnected(const std::string& error);
        void FitToDesktop();
        void OnClose(wxCloseEvent& event);

        ViewerOptions m_options;
        RmVnc::Rfb::Client m_client;
        VncCanvas* m_canvas = nullptr;
        bool m_closing = false;
    };
} // namespace RmVncUI
