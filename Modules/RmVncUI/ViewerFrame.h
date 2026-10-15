// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include <RmVncCore/Address.h>
#include <RmVncCore/Rfb/Client.h>

#include <wx/frame.h>

#include <condition_variable>
#include <mutex>

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
        void OnDisconnected(const RmVnc::Rfb::DisconnectInfo& info) override;
        bool OnCredentialsRequired(bool needUsername, RmVnc::Rfb::Credentials& credentials) override;

        // UI thread
        void Connect();
        void HandleConnected(const RmVnc::Rfb::ServerInit& serverInit);
        void HandleDisconnected(const RmVnc::Rfb::DisconnectInfo& info);
        void AskForCredentials(bool needUsername);
        void FitToDesktop();
        void OnClose(wxCloseEvent& event);
        wxString GetServerDisplayName() const;

        ViewerOptions m_options;
        RmVnc::Rfb::Client m_client;
        VncCanvas* m_canvas = nullptr;
        wxString m_desktopName;

        // Hand-off between the connection thread (waiting for credentials) and the UI thread (asking the user).
        std::mutex m_credentialsMutex;
        std::condition_variable m_credentialsChanged;
        bool m_credentialsAnswered = false;
        bool m_credentialsAccepted = false;
        RmVnc::Rfb::Credentials m_credentialsAnswer;
        bool m_closing = false; // guarded by m_credentialsMutex
    };
} // namespace RmVncUI
