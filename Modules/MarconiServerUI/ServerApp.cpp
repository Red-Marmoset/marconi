// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Starts the server, puts an icon in the system tray, and keeps running until Quit

#include "ServerApp.h"

#include "ServerConfig.h"
#include "ServerController.h"
#include "TrayNotifier.h"
#include "TrayIcon.h"

#include <MarconiCore/Version.h>
#include <MarconiServerCore/IdentityStore.h>
#include <MarconiServerCore/PasswordFile.h>
#include <MarconiServerCore/ControlSocket.h>
#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/Settings.h>
#include <MarconiServerCore/TrayMode.h>
#include <MarconiServerCore/TrayTransportLink.h>

#include <Log/Log.h>

#if defined(__linux__)
#include <MarconiServerCore/X11Support.h>

#include <cstdio>
#include <cstdlib>
#endif

#include <wx/bmpbndl.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>
#include <wx/vector.h>

#include <wx/menu.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>

namespace Marconi
{
    namespace ServerUI
    {
        namespace
        {
            // The tray icon, drawn at runtime so the tray app needs no image file: a blue circle with the white "M" of
            // Marconi's own icon (a mirrored line with rounded joins and a round dot at each end, the same geometry as
            // MarconiUI/AppIcon.cpp, scaled to sit inside the circle). Keep the two in step.
            wxBitmap DrawTrayBitmap(int size)
            {
                wxImage image(size, size);
                image.InitAlpha();
                std::fill_n(image.GetAlpha(), size * size, 0);
                wxBitmap bitmap(image);
                {
                    wxMemoryDC dc(bitmap);
                    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
                    if (gc)
                    {
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(wxColour(0x1E, 0x66, 0xD0)));
                        gc->DrawEllipse(0, 0, size, size);

                        // The main icon's 32-unit grid, with the M scaled about its centre (16, 16) to fit the circle.
                        const double unit = size / 32.0;
                        const double fit = 0.86;
                        auto at = [&](double v) { return (16.0 + (v - 16.0) * fit) * unit; };
                        const wxColour white(255, 255, 255);
                        wxGraphicsPenInfo stroke(white, 4.0 * unit * fit);
                        stroke.Cap(wxCAP_ROUND).Join(wxJOIN_ROUND);
                        gc->SetPen(gc->CreatePen(stroke));
                        gc->SetBrush(*wxTRANSPARENT_BRUSH);
                        wxGraphicsPath signal = gc->CreatePath();
                        signal.MoveToPoint(at(5), at(23));
                        signal.AddLineToPoint(at(10.5), at(9));
                        signal.AddLineToPoint(at(16), at(20));
                        signal.AddLineToPoint(at(21.5), at(9));
                        signal.AddLineToPoint(at(27), at(23));
                        gc->StrokePath(signal);

                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(white));
                        const double radius = 3.0 * unit * fit;
                        for (double x : {5.0, 27.0})
                        {
                            gc->DrawEllipse(at(x) - radius, at(23) - radius, 2 * radius, 2 * radius);
                        }
                    }
                }
                return bitmap;
            }

            // Several sizes, so the system picks a sharp one for its tray (16 px on a normal screen, more on a
            // high-resolution one).
            wxBitmapBundle MakeTrayIcon()
            {
                wxVector<wxBitmap> bitmaps;
                for (int size : {16, 24, 32, 48, 64})
                {
                    bitmaps.push_back(DrawTrayBitmap(size));
                }
                return wxBitmapBundle::FromBitmaps(bitmaps);
            }

            // Asks for an access password, and asks again if it is left empty: a server with no password refuses every
            // viewer. Returns false if the person cancelled. On OK, `hashOut` is the stored hash.
            bool AskForPassword(const wxString& message, std::string& hashOut)
            {
                wxString prompt = message;
                for (;;)
                {
                    wxPasswordEntryDialog dialog(nullptr, prompt, "Marconi Server password");
                    if (dialog.ShowModal() != wxID_OK)
                    {
                        return false;
                    }
                    wxString password = dialog.GetValue();
                    if (password.IsEmpty())
                    {
                        prompt = "A password is needed: without one, nobody can connect.\n\n" + message;
                        continue;
                    }
                    hashOut = Server::HashPassword(password.utf8_string());
                    return true;
                }
            }
        } // namespace

        bool ServerApp::OnInit()
        {
            // A plain lowercase name for the data folder and config; the window and menu text say "Marconi Server".
            SetAppName("marconi-server");
            SetVendorName("Red Marmoset Studios");

            // One tray app per user: a second would fight the first for the port and add a second icon. A command
            // line job (--set-password-file) may still run alongside it.
            bool commandLineJob = false;
            for (int i = 1; i < argc; ++i)
            {
                commandLineJob = commandLineJob || wxString(argv[i]) == "--set-password-file";
            }
            if (!commandLineJob)
            {
                m_instanceChecker = std::make_unique<wxSingleInstanceChecker>();
                if (m_instanceChecker->CreateDefault() && m_instanceChecker->IsAnotherRunning())
                {
                    wxMessageBox("Marconi Server is already running. Look for its icon in the system tray.",
                                 "Marconi Server", wxOK | wxICON_INFORMATION);
                    return false;
                }
            }

            m_config = std::make_unique<ServerConfig>();
            if (m_config->GetDataDirectory().IsEmpty())
            {
                wxMessageBox("Could not create the folder for Marconi Server's settings.", "Marconi Server",
                             wxOK | wxICON_ERROR);
                return false;
            }

            // One log file per run in <data folder>/logs. The installer's --set-password-file runs get their own
            // files, so they can't push the tray app's own logs out of the kept ones.
            bool setPasswordRun = false;
            for (int i = 1; i + 1 < argc; ++i)
            {
                setPasswordRun = setPasswordRun || wxString(argv[i]) == "--set-password-file";
            }
            m_logFile.Open(m_config->GetLogDirectory().utf8_string(),
                           setPasswordRun ? "MarconiServerSetPassword" : "MarconiServerTray");
            LOG_INFO("Server", "Marconi Server %s starting", GetVersionString());

            for (int i = 1; i + 1 < argc; ++i)
            {
                if (wxString(argv[i]) == "--set-password-file")
                {
                    m_commandOnly = true;
                    m_commandResult = SetPasswordFromFile(argv[i + 1]);
                    return true; // OnRun returns the result without starting anything
                }
            }

            // Which way to reach the Windows service's helper: its named pipe, or (developer option, macOS and Linux)
            // a Unix socket that `MarconiServer --control-socket` serves.
            std::string controlSocket;
            for (int i = 1; i < argc; ++i)
            {
                const wxString arg = argv[i];
                if (arg == "--control-socket" && i + 1 < argc)
                {
                    controlSocket = wxString(argv[i + 1]).utf8_string();
                }
                else if (arg == "--dump-menu")
                {
                    m_dumpMenu = true;
                }
            }
            Server::Tray::TransportFactory factory =
                controlSocket.empty() ? Server::Tray::NamedPipeFactory()
                                      : Server::Tray::TransportFactory([controlSocket](std::string& error) {
                                            return Server::Control::ConnectToControlSocket(controlSocket, error);
                                        });
            m_connector = std::make_unique<Server::Tray::TransportConnector>(
                factory, "tray icon (" + wxGetUserId().utf8_string() + ")");
            // Made before the first poll, which may start the server and hand it the notifier.
            m_notifier = std::make_unique<TrayNotifier>(m_config->GetNotifyEnabled());
            m_trayMode = std::make_unique<Server::Tray::TrayMode>(*m_connector, []() {
                return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count());
            });
            m_trayMode->SetModeChangedHandler(
                [this](Server::Tray::Mode, Server::Tray::Mode to) { OnTrayModeChanged(to); });
            m_trayMode->SetStatusChangedHandler([this]() {
                if (m_dumpMenu)
                {
                    DumpMenu();
                }
            });

            // Ask the helper first. If it answers, this program only shows its status and starts no listener of its
            // own (it would fight the service for the port); if nothing answers, it is the server, as before. The
            // first Tick polls straight away and the mode handler starts the server when that is what is wanted.
            m_trayMode->Tick();
            if (m_ownServerFailed)
            {
                return false;
            }

            m_tray = new TrayIcon(*m_trayMode, *m_config, [this]() { return m_controller.get(); }, *m_notifier);
            if (!m_tray->SetIcon(MakeTrayIcon(), "Marconi Server"))
            {
                LOG_WARNING("Server", "Could not put the icon in the system tray");
            }

            // There is no window; the tray icon keeps the program running until Quit.
            SetExitOnFrameDelete(false);

            // Poll the helper every few seconds (TrayMode decides when a poll is due), so the tray icon switches mode
            // if the service starts or stops later.
            // (Not while the server is being set up: its first-run and problem dialogs run a nested event loop, and a
            // mode change in the middle of that would start or stop the server under it.)
            m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                if (!m_startingOwnServer)
                {
                    m_trayMode->Tick();
                }
            });
            m_timer.Start(1000);

            m_initialised = true;
            if (m_dumpMenu)
            {
                DumpMenu();
            }
            return true;
        }

        void ServerApp::OnTrayModeChanged(Server::Tray::Mode mode)
        {
            LOG_INFO("Server", "Tray mode: %s", Server::Tray::ModeName(mode));
            if (m_dumpMenu)
            {
                printf("MODE %s\n", Server::Tray::ModeName(mode));
                fflush(stdout);
            }
            if (mode == Server::Tray::Mode::OwnServer)
            {
                if (!StartOwnServer())
                {
                    m_ownServerFailed = true;
                }
            }
            else
            {
                StopOwnServer(); // a helper is serving now: leave the port to it
            }
        }

        void ServerApp::StopOwnServer()
        {
            if (m_controller)
            {
                m_controller->Stop();
                m_controller.reset();
            }
        }

        bool ServerApp::StartOwnServer()
        {
            struct Busy
            {
                explicit Busy(bool& flag) : m_flag(flag) { m_flag = true; }
                ~Busy() { m_flag = false; }
                bool& m_flag;
            } busy(m_startingOwnServer);
            // Build the settings from what's saved, and make or load the lasting TLS identity so viewers aren't asked
            // to trust the server again after every restart.
            Server::Settings settings;
            settings.serverVersion = GetVersionString();
            settings.serverName = wxGetHostName().utf8_string();
            settings.port = m_config->GetListenPort();
            settings.passwordHash = m_config->GetPasswordHash().utf8_string();
            settings.filesEnabled = m_config->GetFilesEnabled();
            settings.allowedAddresses = m_config->GetAllowedAddresses();
            settings.idleMinutes = m_config->GetIdleMinutes();

            std::string identityError;
            if (!Server::LoadOrCreateIdentity(m_config->GetCertificatePath().utf8_string(),
                                              m_config->GetKeyPath().utf8_string(), settings.serverName, settings,
                                              identityError))
            {
                wxMessageBox(wxString::Format("Could not set up encryption: %s",
                                              wxString::FromUTF8(identityError.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
                return false;
            }

            // First run: ask for a password once. Until one is set the server refuses every viewer. After this the tray
            // menu changes it.
            if (m_config->IsFirstRun())
            {
                std::string hash;
                if (AskForPassword("Welcome to Marconi Server.\n\nSet an access password. Viewers are asked for it when "
                                   "they connect, and nobody can connect until you set one.",
                                   hash))
                {
                    settings.passwordHash = hash;
                    m_config->SetPasswordHash(wxString::FromUTF8(hash.c_str()));
                }
                else
                {
                    m_config->MarkConfigured(); // they dismissed it; don't nag every launch
                }
            }

            m_controller = std::make_unique<ServerController>(settings);
            m_controller->SetNotifier(m_notifier.get());
            std::string startError;
            if (!m_controller->Start(startError))
            {
                wxMessageBox(wxString::Format("The server could not start on port %u: %s\n\nUse the tray menu to "
                                              "choose another port.",
                                              static_cast<unsigned>(settings.port),
                                              wxString::FromUTF8(startError.c_str())),
                             "Marconi Server", wxOK | wxICON_WARNING);
            }

            // If the screen can't be shared or controlled (a permission still to be given on a Mac, a Wayland
            // desktop on Linux), say so now: the tray menu is easy to miss. The server keeps running meanwhile.
            std::string problem = m_controller->GetProblem();
            if (!problem.empty())
            {
                if (m_dumpMenu)
                {
                    printf("PROBLEM %s\n", problem.c_str());
                    fflush(stdout);
                }
                wxMessageBox(wxString::FromUTF8(problem.c_str()), "Marconi Server", wxOK | wxICON_INFORMATION);
            }
            return true;
        }

        void ServerApp::DumpMenu()
        {
            if (!m_tray)
            {
                return;
            }
            wxMenu* menu = m_tray->CreatePopupMenu();
            printf("MENU");
            for (wxMenuItem* item : menu->GetMenuItems())
            {
                if (item->IsSeparator())
                {
                    printf(" | -");
                }
                else
                {
                    printf(" | %s%s", item->GetItemLabelText().utf8_str().data(), item->IsEnabled() ? "" : " (disabled)");
                }
            }
            printf("\n");
            fflush(stdout);
            delete menu;
        }

        int ServerApp::SetPasswordFromFile(const wxString& path)
        {
            std::string password;
            std::string error;
            if (!Server::ReadPasswordFile(path.utf8_string(), password, error))
            {
                LOG_WARNING("Server", "Could not set the password from a file: %s", error.c_str());
                return 1;
            }
            // Hash first and erase the file straight away, so the plain password sits on disk for as short a time as
            // possible; the hash is what is stored.
            std::string hash = Server::HashPassword(password);
            password.assign(password.size(), '\0');
            if (!Server::ErasePasswordFile(path.utf8_string()))
            {
                LOG_WARNING("Server", "The password file could not be removed");
            }
            if (hash.empty())
            {
                LOG_WARNING("Server", "The password could not be hashed");
                return 1;
            }
            m_config->SetPasswordHash(wxString::FromUTF8(hash.c_str()));
            LOG_INFO("Server", "The access password was set from a file");
            return 0;
        }

        int ServerApp::OnRun()
        {
            if (m_commandOnly)
            {
                return m_commandResult;
            }
            return wxApp::OnRun();
        }

        int ServerApp::OnExit()
        {
            m_timer.Stop();
            StopOwnServer();
            if (m_tray)
            {
                m_tray->RemoveIcon();
                m_tray->Destroy();
                m_tray = nullptr;
            }
            LOG_INFO("Server", "Marconi Server stopped");
            m_logFile.Close();
            return wxApp::OnExit();
        }
    } // namespace ServerUI
} // namespace Marconi

#if defined(__linux__)
// Without a desktop, wxWidgets would end with a one-line message about "the X Display". Say it in plain words
// first. (Wayland desktops get past this: the tray works, and the server explains in its own message that the
// screen can't be shared.)
wxIMPLEMENT_APP_NO_MAIN(Marconi::ServerUI::ServerApp);

int main(int argc, char** argv)
{
    const char* x11 = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if ((x11 == nullptr || *x11 == '\0') && (wayland == nullptr || *wayland == '\0'))
    {
        std::fprintf(stderr, "%s\n", Marconi::Server::DescribeNoX11Display().c_str());
        std::fprintf(stderr, "To run the server without a desktop, use the console program (MarconiServer).\n");
        return 1;
    }
    return wxEntry(argc, argv);
}
#else
wxIMPLEMENT_APP(Marconi::ServerUI::ServerApp);
#endif
