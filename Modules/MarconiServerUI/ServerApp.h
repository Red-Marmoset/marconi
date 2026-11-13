// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for the Marconi server tray program

#pragma once

#include <memory>

#include <MarconiServerCore/ServerLog.h>

#include <wx/app.h>
#include <wx/timer.h>
#include <wx/snglinst.h>

namespace Marconi
{
    namespace ServerUI
    {
        class ServerConfig;
        class ServerController;
        class TrayIcon;
        class TrayNotifier;
        class TrayUpdateApprover;
    } // namespace ServerUI
    namespace Server
    {
        namespace Tray
        {
            class TrayMode;
            class TransportConnector;
            enum class Mode;
        } // namespace Tray
    } // namespace Server
    namespace ServerUI
    {

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

            // The tray icon is either the server itself (nothing answers on the control pipe) or only a status window
            // for the Windows service's helper (something does); see Server::Tray::TrayMode.
            bool StartOwnServer();
            void StopOwnServer();
            void OnTrayModeChanged(Server::Tray::Mode mode);
            void DumpMenu(); // --dump-menu: prints the menu's labels, for checking the tray without clicking it

            bool m_commandOnly = false; // ran a command line job: no tray icon, no server
            int m_commandResult = 0;
            // Held while the tray app runs, so a second copy started by the same user sees it and quits.
            std::unique_ptr<wxSingleInstanceChecker> m_instanceChecker;
            std::unique_ptr<ServerConfig> m_config;
            Server::ServerLogFile m_logFile; // one file per run in <data folder>/logs
            std::unique_ptr<TrayNotifier> m_notifier; // declared before the controller so it outlives it
            std::unique_ptr<TrayUpdateApprover> m_updateApprover; // the same: asks before a viewer updates the server
            std::unique_ptr<ServerController> m_controller;
            std::unique_ptr<Server::Tray::TransportConnector> m_connector;
            std::unique_ptr<Server::Tray::TrayMode> m_trayMode;
            wxTimer m_timer;            // asks the TrayMode to poll
            bool m_dumpMenu = false;
            bool m_startingOwnServer = false; // inside StartOwnServer, where the timer must not poll
            bool m_ownServerFailed = false; // StartOwnServer could not set up encryption
            bool m_initialised = false;     // OnInit finished
            TrayIcon* m_tray = nullptr; // owned by wx once created; removed in OnExit
        };
    } // namespace ServerUI
} // namespace Marconi

wxDECLARE_APP(Marconi::ServerUI::ServerApp);
