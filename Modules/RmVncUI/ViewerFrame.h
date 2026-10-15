// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include "ClipboardSync.h"
#include "VncCanvas.h"

#include <RmVncCore/Address.h>
#include <RmVncCore/Rfb/Client.h>

#include <wx/frame.h>

class wxMenu;
class wxToolBar;

#include <condition_variable>
#include <mutex>

namespace RmVncUI
{
    struct ViewerOptions
    {
        RmVnc::Address address;
        RmVnc::Rfb::Credentials credentials;
        ScaleMode scaleMode = ScaleMode::Fit;
        bool viewOnly = false;
        bool fullScreen = false;
        bool clipboard = true;
        wxString savedId; // the saved connection this window was opened from, if any
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
        void OnCursorShape(const RmVnc::Rfb::CursorShape& cursor) override;
        void OnServerCutText(const std::string& latin1Text) override;
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

        ViewerOptions m_options;
        RmVnc::Rfb::Client m_client;
        ClipboardSync m_clipboard{m_client};
        VncCanvas* m_canvas = nullptr;
        wxToolBar* m_toolBar = nullptr;
        wxString m_desktopName;
        wxSize m_connectedSize;
        bool m_connected = false;
        bool m_rememberPassword = false; // the user ticked "remember" at the prompt: save once logged in

        // Hand-off between the connection thread (waiting for credentials) and the UI thread (asking the user).
        std::mutex m_credentialsMutex;
        std::condition_variable m_credentialsChanged;
        bool m_credentialsAnswered = false;
        bool m_credentialsAccepted = false;
        RmVnc::Rfb::Credentials m_credentialsAnswer;
        bool m_closing = false; // guarded by m_credentialsMutex
    };
} // namespace RmVncUI
