// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon and its menu

#include "TrayIcon.h"

#include "ServerConfig.h"
#include "ServerController.h"

#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/TrayMode.h>

#include <Log/Log.h>

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace Marconi
{
    namespace ServerUI
    {
        namespace
        {
            enum
            {
                ID_SetPassword = wxID_HIGHEST + 1,
                ID_ChangePort,
                ID_AllowFiles,
                ID_CopyFingerprint,
                ID_ShowProblem,
                ID_CheckAgain,
                ID_OpenScreenSettings,
                ID_OpenControlSettings,
                ID_DisconnectFirst,
                ID_DisconnectLast = ID_DisconnectFirst + 15, // one per connected viewer (there is one at a time for now)
            };
        } // namespace

        wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
            EVT_MENU(ID_SetPassword, TrayIcon::OnSetPassword)
            EVT_MENU(ID_ChangePort, TrayIcon::OnChangePort)
            EVT_MENU(ID_AllowFiles, TrayIcon::OnAllowFiles)
            EVT_MENU(ID_CopyFingerprint, TrayIcon::OnCopyFingerprint)
            EVT_MENU(ID_ShowProblem, TrayIcon::OnShowProblem)
            EVT_MENU(ID_CheckAgain, TrayIcon::OnCheckAgain)
            EVT_MENU(ID_OpenScreenSettings, TrayIcon::OnOpenScreenSettings)
            EVT_MENU(ID_OpenControlSettings, TrayIcon::OnOpenControlSettings)
            EVT_MENU_RANGE(ID_DisconnectFirst, ID_DisconnectLast, TrayIcon::OnDisconnectViewer)
            EVT_MENU(wxID_EXIT, TrayIcon::OnQuit)
        wxEND_EVENT_TABLE()

        TrayIcon::TrayIcon(Server::Tray::TrayMode& mode, ServerConfig& config,
                           std::function<ServerController*()> controller)
            : m_mode(mode), m_config(config), m_getController(std::move(controller))
        {
        }

        // The menu when a helper (the Windows service) is serving: what it reports, Disconnect viewer, Set password.
        // Built from Server::Tray::StatusOnlyMenu, which the tests check; no listener is run from here.
        wxMenu* TrayIcon::CreateStatusOnlyMenu()
        {
            wxMenu* menu = new wxMenu();
            m_menuViewerIds.clear();
            m_menuFingerprint = m_mode.GetSnapshot().status.fingerprint;
            for (const Server::Tray::MenuItem& item : Server::Tray::StatusOnlyMenu(m_mode.GetSnapshot()))
            {
                const wxString label = wxString::FromUTF8(item.label.c_str());
                switch (item.kind)
                {
                case Server::Tray::ItemKind::Info:
                    menu->Append(wxID_ANY, label)->Enable(false);
                    break;
                case Server::Tray::ItemKind::Separator:
                    menu->AppendSeparator();
                    break;
                case Server::Tray::ItemKind::SetPassword:
                    menu->Append(ID_SetPassword, label);
                    break;
                case Server::Tray::ItemKind::CopyFingerprint:
                    menu->Append(ID_CopyFingerprint, label);
                    break;
                case Server::Tray::ItemKind::DisconnectViewer:
                    if (m_menuViewerIds.size() < static_cast<size_t>(ID_DisconnectLast - ID_DisconnectFirst + 1))
                    {
                        menu->Append(ID_DisconnectFirst + static_cast<int>(m_menuViewerIds.size()), label);
                        m_menuViewerIds.push_back(item.viewerId);
                    }
                    break;
                case Server::Tray::ItemKind::Quit:
                    menu->Append(wxID_EXIT, label);
                    break;
                }
            }
            return menu;
        }

        void TrayIcon::OnDisconnectViewer(wxCommandEvent& event)
        {
            const size_t index = static_cast<size_t>(event.GetId() - ID_DisconnectFirst);
            if (index >= m_menuViewerIds.size())
            {
                return;
            }
            Server::Tray::ActionResult result = m_mode.DisconnectViewer(m_menuViewerIds[index]);
            if (!result.ok)
            {
                wxMessageBox(wxString::FromUTF8(result.message.c_str()), "Marconi Server", wxOK | wxICON_ERROR);
            }
        }

        // (The ellipses are built with FromUTF8: a plain narrow literal with one in it comes out as an empty label,
        // and wxWidgets asserts, when the program's locale is not UTF-8.)
        wxMenu* TrayIcon::CreatePopupMenu()
        {
            const Server::Tray::Mode mode = m_mode.GetMode();
            if (mode == Server::Tray::Mode::StatusOnly || mode == Server::Tray::Mode::ServiceUnreadable)
            {
                return CreateStatusOnlyMenu();
            }
            if (!m_getController())
            {
                // This program is meant to be the server but has none (it could not be set up): only Quit works.
                wxMenu* bare = new wxMenu();
                bare->Append(wxID_ANY, "Marconi Server is not running")->Enable(false);
                bare->AppendSeparator();
                bare->Append(wxID_EXIT, "Quit Marconi Server");
                return bare;
            }
            wxMenu* menu = new wxMenu();

            // A few lines of status at the top, all disabled: what's being served, the port, whether a password is
            // needed, and a short piece of the fingerprint.
            wxString served = wxString::FromUTF8(Controller().GetServedDescription().c_str());
            wxString running = Controller().IsRunning()
                                   ? wxString::Format("Sharing %s", served)
                                   : wxString("Server stopped");
            menu->Append(wxID_ANY, running)->Enable(false);
            menu->Append(wxID_ANY, wxString::Format("Port %u", static_cast<unsigned>(Controller().GetBoundPort())))
                ->Enable(false);
            bool hasPassword = !m_config.GetPasswordHash().IsEmpty();
            menu->Append(wxID_ANY, hasPassword ? "Password required" : "No password set: viewers can't connect")->Enable(false);

            wxString fingerprint = wxString::FromUTF8(Controller().GetFingerprint().c_str());
            if (!fingerprint.IsEmpty())
            {
                wxString shortPrint = fingerprint.Left(16) + wxString::FromUTF8("\xE2\x80\xA6"); // first 16 hex digits then an ellipsis
                menu->Append(wxID_ANY, "Fingerprint " + shortPrint)->Enable(false);
                menu->Append(ID_CopyFingerprint, "Copy full fingerprint");
            }

            // When the screen can't be shared or controlled, say so here; clicking it shows the full explanation.
            if (!Controller().GetProblem().empty())
            {
                menu->AppendSeparator();
                menu->Append(ID_ShowProblem, wxString::FromUTF8("Problem: click for details…"));
                menu->Append(ID_CheckAgain, "Check again");
#ifdef __WXOSX__
                menu->Append(ID_OpenScreenSettings, wxString::FromUTF8("Open Screen Recording settings…"));
                menu->Append(ID_OpenControlSettings, wxString::FromUTF8("Open Accessibility settings…"));
#endif
            }

            menu->AppendSeparator();
            menu->Append(ID_SetPassword, wxString::FromUTF8(hasPassword ? "Change password…" : "Set password…"));
            menu->Append(ID_ChangePort, wxString::FromUTF8("Change port…"));
            menu->AppendCheckItem(ID_AllowFiles, "Let viewers copy files")->Check(m_config.GetFilesEnabled());
            menu->AppendSeparator();
            menu->Append(wxID_EXIT, "Quit Marconi Server");
            return menu;
        }

        void TrayIcon::OnSetPassword(wxCommandEvent&)
        {
            const bool viaHelper = m_mode.GetMode() == Server::Tray::Mode::StatusOnly;
            wxPasswordEntryDialog dialog(nullptr,
                                         "Enter an access password for this server.\nViewers are asked for it when "
                                         "they connect, and nobody can connect without one.",
                                         "Marconi Server password");
            if (dialog.ShowModal() != wxID_OK)
            {
                return;
            }
            wxString password = dialog.GetValue();
            if (password.IsEmpty())
            {
                wxMessageBox("A password is needed: without one, nobody can connect. The password was not changed.",
                             "Marconi Server", wxOK | wxICON_INFORMATION);
                return;
            }

            std::string hash;
            {
                hash = Server::HashPassword(password.utf8_string());
                if (hash.empty())
                {
                    wxMessageBox("Could not secure the password. Please try again.", "Marconi Server",
                                 wxOK | wxICON_ERROR);
                    return;
                }
            }

            if (viaHelper)
            {
                // The service owns the password: ask it over the pipe. It checks that this user is an administrator,
                // and says so in its answer when not. Nothing is kept in this user's own settings.
                Server::Tray::ActionResult result = m_mode.SetPasswordHash(hash);
                wxMessageBox(wxString::FromUTF8(result.message.c_str()), "Marconi Server",
                             result.ok ? (wxOK | wxICON_INFORMATION) : (wxOK | wxICON_ERROR));
                return;
            }

            std::string error;
            if (!Controller().ChangePasswordHash(hash, error))
            {
                wxMessageBox(wxString::Format("The password was saved, but the server could not restart: %s",
                                              wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
            }
            m_config.SetPasswordHash(wxString::FromUTF8(hash.c_str()));
        }

        void TrayIcon::OnAllowFiles(wxCommandEvent&)
        {
            const bool enable = !m_config.GetFilesEnabled();
            if (enable)
            {
                const int answer = wxMessageBox(
                    "Anyone who connects with the access password will be able to browse, copy, change and delete "
                    "files on this computer.\n\nAllow it?",
                    "Marconi Server", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
                if (answer != wxYES)
                {
                    return;
                }
            }
            std::string error;
            if (!Controller().ChangeFilesEnabled(enable, error))
            {
                wxMessageBox(wxString::Format("The setting was saved, but the server could not restart: %s",
                                              wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
            }
            m_config.SetFilesEnabled(enable);
        }

        void TrayIcon::OnChangePort(wxCommandEvent&)
        {
            long current = Controller().GetBoundPort();
            long port = wxGetNumberFromUser("Which port should the server listen on?", "Port", "Marconi Server",
                                            current, 1, 65535);
            if (port < 1)
            {
                return; // cancelled
            }
            std::string error;
            if (!Controller().ChangePort(static_cast<uint16_t>(port), error))
            {
                wxMessageBox(wxString::Format("Could not listen on port %ld: %s", port,
                                              wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
                return;
            }
            m_config.SetPort(static_cast<uint16_t>(port));
        }

        void TrayIcon::OnCopyFingerprint(wxCommandEvent&)
        {
            wxString fingerprint = m_getController() ? wxString::FromUTF8(Controller().GetFingerprint().c_str())
                                                     : wxString::FromUTF8(m_menuFingerprint.c_str());
            if (fingerprint.IsEmpty() || !wxTheClipboard->Open())
            {
                return;
            }
            wxTheClipboard->SetData(new wxTextDataObject(fingerprint));
            wxTheClipboard->Close();
        }

        void TrayIcon::OnShowProblem(wxCommandEvent&)
        {
            ShowProblem();
        }

        void TrayIcon::ShowProblem()
        {
            wxMessageBox(wxString::FromUTF8(Controller().GetProblem().c_str()), "Marconi Server",
                         wxOK | wxICON_INFORMATION);
        }

        void TrayIcon::OnCheckAgain(wxCommandEvent&)
        {
            std::string error;
            if (!Controller().CheckScreenAndInputAgain(error))
            {
                wxMessageBox(wxString::Format("The server could not start: %s", wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
                return;
            }
            if (!Controller().GetProblem().empty())
            {
                ShowProblem();
            }
        }

        // These open the page of the macOS settings where the person allows Marconi Server.
        void TrayIcon::OnOpenScreenSettings(wxCommandEvent&)
        {
            wxLaunchDefaultBrowser("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture");
        }

        void TrayIcon::OnOpenControlSettings(wxCommandEvent&)
        {
            wxLaunchDefaultBrowser("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
        }

        void TrayIcon::OnQuit(wxCommandEvent&)
        {
            if (m_getController())
            {
                Controller().Stop();
            }
            RemoveIcon();
            wxTheApp->ExitMainLoop(); // no windows are open, so end the event loop directly; OnExit cleans up
        }
    } // namespace ServerUI
} // namespace Marconi
