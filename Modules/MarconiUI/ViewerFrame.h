// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Window for one VNC connection: owns the Client and forwards its callbacks to the UI thread

#pragma once

#include "ClipboardSync.h"
#include "VncCanvas.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Rfb/Client.h>

#include <wx/frame.h>
#include <wx/timer.h>

class wxMenu;
class wxToolBar;

#include <condition_variable>
#include <mutex>

namespace MarconiUI
{
    struct ViewerOptions
    {
        Marconi::Address address;
        Marconi::Rfb::Credentials credentials;
        ScaleMode scaleMode = ScaleMode::Fit;
        bool viewOnly = false;
        bool fullScreen = false;
        bool clipboard = true;
        wxString savedId; // the saved connection this window was opened from, if any
    };

    class ViewerFrame : public wxFrame, private Marconi::Rfb::ClientListener
    {
    public:
        explicit ViewerFrame(const ViewerOptions& options);
        ~ViewerFrame() override;

    private:
        // ClientListener (connection thread)
        void OnConnected(const Marconi::Rfb::ServerInit& serverInit) override;
        void OnFramebufferUpdated(const std::vector<Marconi::Rfb::Rect>& rects) override;
        void OnDesktopResized(int width, int height) override;
        void OnBell() override;
        void OnCursorShape(const Marconi::Rfb::CursorShape& cursor) override;
        void OnServerCutText(const std::string& latin1Text) override;
        void OnDisconnected(const Marconi::Rfb::DisconnectInfo& info) override;
        bool OnCredentialsRequired(bool needUsername, Marconi::Rfb::Credentials& credentials) override;

        // UI thread
        void Connect();
        void HandleConnected(const Marconi::Rfb::ServerInit& serverInit);
        void HandleDisconnected(const Marconi::Rfb::DisconnectInfo& info);
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
        void CheckForPicture();

        ViewerOptions m_options;
        Marconi::Rfb::Client m_client;
        ClipboardSync m_clipboard{m_client};
        VncCanvas* m_canvas = nullptr;
        wxToolBar* m_toolBar = nullptr;
        wxString m_desktopName;
        wxSize m_connectedSize;
        bool m_connected = false;
        wxTimer m_pictureTimer;   // re-asks for the desktop if a fresh connection still shows nothing
        int m_pictureRetries = 0;
        bool m_rememberPassword = false; // the user ticked "remember" at the prompt: save once logged in

        // Hand-off between the connection thread (waiting for credentials) and the UI thread (asking the user).
        std::mutex m_credentialsMutex;
        std::condition_variable m_credentialsChanged;
        bool m_credentialsAnswered = false;
        bool m_credentialsAccepted = false;
        Marconi::Rfb::Credentials m_credentialsAnswer;
        bool m_closing = false; // guarded by m_credentialsMutex
    };
} // namespace MarconiUI
