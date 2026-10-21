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
            int OnExit() override;

        private:
            std::unique_ptr<ServerConfig> m_config;
            std::unique_ptr<ServerController> m_controller;
            TrayIcon* m_tray = nullptr; // owned by wx once created; removed in OnExit
        };
    } // namespace ServerUI
} // namespace Marconi

wxDECLARE_APP(Marconi::ServerUI::ServerApp);
