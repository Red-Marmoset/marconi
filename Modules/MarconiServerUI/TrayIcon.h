// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The system tray icon for the Marconi server: shows whether it's running and lets you set the password, change the
// port, and quit

#pragma once

#include <wx/taskbar.h>

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace Marconi
{
    namespace ServerUI
    {
        class ServerConfig;
        class ServerController;
    } // namespace ServerUI
    namespace Server
    {
        namespace Tray
        {
            class TrayMode;
        }
    } // namespace Server
    namespace ServerUI
    {

        class TrayIcon : public wxTaskBarIcon
        {
        public:
            // `controller` gives the server this program runs itself, or null when it doesn't (a helper is serving and this is
            // only its status window, or the server could not start). `mode` decides which of the two the menu shows.
            TrayIcon(Server::Tray::TrayMode& mode, ServerConfig& config,
                     std::function<ServerController*()> controller);

            // wxTaskBarIcon asks for the menu each time it's clicked; we build a fresh one so the status lines are
            // current.
            wxMenu* CreatePopupMenu() override;

        private:
            wxMenu* CreateStatusOnlyMenu();
            void OnDisconnectViewer(wxCommandEvent& event);
            void OnSetPasswordViaHelper();
            void ShowProblem();
            ServerController& Controller() { return *m_getController(); }
            void OnSetPassword(wxCommandEvent& event);
            void OnAllowFiles(wxCommandEvent& event);
            void OnChangePort(wxCommandEvent& event);
            void OnCopyFingerprint(wxCommandEvent& event);
            void OnShowProblem(wxCommandEvent& event);
            void OnCheckAgain(wxCommandEvent& event);
            void OnOpenScreenSettings(wxCommandEvent& event);
            void OnOpenControlSettings(wxCommandEvent& event);
            void OnQuit(wxCommandEvent& event);

            Server::Tray::TrayMode& m_mode;
            ServerConfig& m_config;
            std::function<ServerController*()> m_getController;
            std::vector<uint32_t> m_menuViewerIds; // the viewer behind each Disconnect item of the last menu
            std::string m_menuFingerprint;         // the full fingerprint behind "Copy full fingerprint" (status-only)

            wxDECLARE_EVENT_TABLE();
        };
    } // namespace ServerUI
} // namespace Marconi
