// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for the Marconi server tray program

#pragma once

#include <memory>

#include <wx/app.h>

namespace Marconi
{
    namespace ServerUI
    {
        class ServerConfig;
        class ServerController;
        class TrayIcon;

        class ServerApp : public wxApp
        {
        public:
            bool OnInit() override;
            int OnRun() override;
            int OnExit() override;

        private:
            // --set-password-file <file>: stores the access password in the file (the installer's way of setting
            // it, so it never goes on a command line), erases the file and exits. The result is the exit code.
            int SetPasswordFromFile(const wxString& path);

            bool m_commandOnly = false; // ran a command line job: no tray icon, no server
            int m_commandResult = 0;
            std::unique_ptr<ServerConfig> m_config;
            std::unique_ptr<ServerController> m_controller;
            TrayIcon* m_tray = nullptr; // owned by wx once created; removed in OnExit
        };
    } // namespace ServerUI
} // namespace Marconi

wxDECLARE_APP(Marconi::ServerUI::ServerApp);
