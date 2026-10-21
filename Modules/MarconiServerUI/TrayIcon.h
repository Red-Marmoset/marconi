// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The system tray icon for the Marconi server: shows whether it's running and lets you set the password, change the
// port, and quit

#pragma once

#include <wx/taskbar.h>

namespace Marconi
{
    namespace ServerUI
    {
        class ServerConfig;
        class ServerController;

        class TrayIcon : public wxTaskBarIcon
        {
        public:
            TrayIcon(ServerController& controller, ServerConfig& config);

            // wxTaskBarIcon asks for the menu each time it's clicked; we build a fresh one so the status lines are
            // current.
            wxMenu* CreatePopupMenu() override;

        private:
            void OnSetPassword(wxCommandEvent& event);
            void OnChangePort(wxCommandEvent& event);
            void OnCopyFingerprint(wxCommandEvent& event);
            void OnQuit(wxCommandEvent& event);

            ServerController& m_controller;
            ServerConfig& m_config;

            wxDECLARE_EVENT_TABLE();
        };
    } // namespace ServerUI
} // namespace Marconi
