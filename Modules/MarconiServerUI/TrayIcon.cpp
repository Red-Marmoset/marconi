// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon and its menu

#include "TrayIcon.h"

#include "ServerConfig.h"
#include "ServerController.h"
#include "TrayNotifier.h"

#include <MarconiServerCore/AllowList.h>

#include <MarconiServerCore/PasswordHash.h>

#include <Log/Log.h>

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/notifmsg.h>
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
                ID_Notify,
                ID_WhoCanConnect,
                ID_IdleFirst,
                ID_IdleLast = ID_IdleFirst + 15,
                ID_CopyFingerprint,
                ID_ShowProblem,
                ID_CheckAgain,
                ID_OpenScreenSettings,
                ID_OpenControlSettings,
            };

            // The choices in "Disconnect idle viewers after": minutes, 0 meaning never.
            struct IdleChoice
            {
                int minutes;
                const char* label;
            };
            const IdleChoice kIdleChoices[] = {{0, "Never"},        {5, "5 minutes"},  {15, "15 minutes"},
                                               {30, "30 minutes"},  {60, "1 hour"},    {240, "4 hours"}};
            const int kIdleChoiceCount = static_cast<int>(sizeof(kIdleChoices) / sizeof(kIdleChoices[0]));
        } // namespace

        wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
            EVT_MENU(ID_SetPassword, TrayIcon::OnSetPassword)
            EVT_MENU(ID_ChangePort, TrayIcon::OnChangePort)
            EVT_MENU(ID_AllowFiles, TrayIcon::OnAllowFiles)
            EVT_MENU(ID_Notify, TrayIcon::OnNotify)
            EVT_MENU(ID_WhoCanConnect, TrayIcon::OnWhoCanConnect)
            EVT_MENU_RANGE(ID_IdleFirst, ID_IdleLast, TrayIcon::OnIdleChoice)
            EVT_MENU(ID_CopyFingerprint, TrayIcon::OnCopyFingerprint)
            EVT_MENU(ID_ShowProblem, TrayIcon::OnShowProblem)
            EVT_MENU(ID_CheckAgain, TrayIcon::OnCheckAgain)
            EVT_MENU(ID_OpenScreenSettings, TrayIcon::OnOpenScreenSettings)
            EVT_MENU(ID_OpenControlSettings, TrayIcon::OnOpenControlSettings)
            EVT_MENU(wxID_EXIT, TrayIcon::OnQuit)
        wxEND_EVENT_TABLE()

        TrayIcon::TrayIcon(ServerController& controller, ServerConfig& config, TrayNotifier& notifier)
            : m_controller(controller), m_config(config), m_notifier(notifier)
        {
#ifdef __WXMSW__
            // Windows shows the notification as a balloon from this icon instead of adding a second one. (Compiled
            // only on Windows, not yet built there.)
            wxNotificationMessage::UseTaskBarIcon(this);
#endif
        }

        wxMenu* TrayIcon::CreatePopupMenu()
        {
            wxMenu* menu = new wxMenu();

            // A few lines of status at the top, all disabled: what's being served, the port, whether a password is
            // needed, and a short piece of the fingerprint.
            wxString served = wxString::FromUTF8(m_controller.GetServedDescription().c_str());
            wxString running = m_controller.IsRunning()
                                   ? wxString::Format("Sharing %s", served)
                                   : wxString("Server stopped");
            menu->Append(wxID_ANY, running)->Enable(false);
            menu->Append(wxID_ANY, wxString::Format("Port %u", static_cast<unsigned>(m_controller.GetBoundPort())))
                ->Enable(false);
            bool hasPassword = !m_config.GetPasswordHash().IsEmpty();
            menu->Append(wxID_ANY, hasPassword ? "Password required" : "No password set: viewers can't connect")->Enable(false);

            wxString fingerprint = wxString::FromUTF8(m_controller.GetFingerprint().c_str());
            if (!fingerprint.IsEmpty())
            {
                wxString shortPrint = fingerprint.Left(16) + "…"; // first 16 hex digits then an ellipsis
                menu->Append(wxID_ANY, "Fingerprint " + shortPrint)->Enable(false);
                menu->Append(ID_CopyFingerprint, "Copy full fingerprint");
            }

            // When the screen can't be shared or controlled, say so here; clicking it shows the full explanation.
            if (!m_controller.GetProblem().empty())
            {
                menu->AppendSeparator();
                menu->Append(ID_ShowProblem, "Problem: click for details…");
                menu->Append(ID_CheckAgain, "Check again");
#ifdef __WXOSX__
                menu->Append(ID_OpenScreenSettings, "Open Screen Recording settings…");
                menu->Append(ID_OpenControlSettings, "Open Accessibility settings…");
#endif
            }

            menu->AppendSeparator();
            menu->Append(ID_SetPassword, hasPassword ? "Change password…" : "Set password…");
            menu->Append(ID_ChangePort, "Change port…");
            menu->AppendCheckItem(ID_AllowFiles, "Let viewers copy files")->Check(m_config.GetFilesEnabled());
            menu->AppendSeparator();
            menu->Append(ID_WhoCanConnect, m_config.GetAllowedAddresses().empty()
                                               ? "Who can connect… (anyone with the password)"
                                               : "Who can connect… (only some computers)");
            wxMenu* idleMenu = new wxMenu();
            const int idleNow = m_config.GetIdleMinutes();
            for (int i = 0; i < kIdleChoiceCount; ++i)
            {
                idleMenu->AppendRadioItem(ID_IdleFirst + i, kIdleChoices[i].label)->Check(kIdleChoices[i].minutes == idleNow);
            }
            menu->AppendSubMenu(idleMenu, "Disconnect idle viewers after");
            menu->AppendCheckItem(ID_Notify, "Tell me when someone connects")->Check(m_notifier.IsEnabled());
            menu->AppendSeparator();
            menu->Append(wxID_EXIT, "Quit Marconi Server");
            return menu;
        }

        void TrayIcon::OnSetPassword(wxCommandEvent&)
        {
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

            std::string error;
            if (!m_controller.ChangePasswordHash(hash, error))
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
            if (!m_controller.ChangeFilesEnabled(enable, error))
            {
                wxMessageBox(wxString::Format("The setting was saved, but the server could not restart: %s",
                                              wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
            }
            m_config.SetFilesEnabled(enable);
        }

        void TrayIcon::OnChangePort(wxCommandEvent&)
        {
            long current = m_controller.GetBoundPort();
            long port = wxGetNumberFromUser("Which port should the server listen on?", "Port", "Marconi Server",
                                            current, 1, 65535);
            if (port < 1)
            {
                return; // cancelled
            }
            std::string error;
            if (!m_controller.ChangePort(static_cast<uint16_t>(port), error))
            {
                wxMessageBox(wxString::Format("Could not listen on port %ld: %s", port,
                                              wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
                return;
            }
            m_config.SetPort(static_cast<uint16_t>(port));
        }

        void TrayIcon::OnNotify(wxCommandEvent&)
        {
            const bool enable = !m_notifier.IsEnabled();
            m_notifier.SetEnabled(enable);
            m_config.SetNotifyEnabled(enable);
        }

        void TrayIcon::OnWhoCanConnect(wxCommandEvent&)
        {
            wxString current;
            for (const std::string& entry : m_config.GetAllowedAddresses())
            {
                current += wxString::FromUTF8(entry.c_str()) + "\n";
            }
            wxTextEntryDialog dialog(nullptr,
                                     "Only computers at these addresses may connect. Type one address or network on "
                                     "each line, for example\n192.168.1.20   or   192.168.1.0/24   (everything on "
                                     "that home network).\n\nLeave it empty to let anyone who can reach this "
                                     "computer try to connect. They still need the password either way.",
                                     "Who can connect", current, wxOK | wxCANCEL | wxTE_MULTILINE);
            dialog.SetSize(wxSize(520, 340));
            if (dialog.ShowModal() != wxID_OK)
            {
                return;
            }
            std::vector<std::string> entries = Server::SplitAllowEntries(dialog.GetValue().utf8_string());
            std::string error;
            if (!m_controller.ChangeAllowedAddresses(entries, error))
            {
                wxMessageBox(wxString::FromUTF8(error.c_str()) + "\n\nThe list was not changed.", "Marconi Server",
                             wxOK | wxICON_ERROR);
                return;
            }
            m_config.SetAllowedAddresses(entries);
            if (!entries.empty())
            {
                wxMessageBox("Only the listed addresses can connect from now on. A viewer that is already connected "
                             "stays connected.",
                             "Marconi Server", wxOK | wxICON_INFORMATION);
            }
        }

        void TrayIcon::OnIdleChoice(wxCommandEvent& event)
        {
            const int index = event.GetId() - ID_IdleFirst;
            if (index < 0 || index >= kIdleChoiceCount)
            {
                return;
            }
            m_controller.ChangeIdleMinutes(kIdleChoices[index].minutes);
            m_config.SetIdleMinutes(kIdleChoices[index].minutes);
        }

        void TrayIcon::OnCopyFingerprint(wxCommandEvent&)
        {
            wxString fingerprint = wxString::FromUTF8(m_controller.GetFingerprint().c_str());
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
            wxMessageBox(wxString::FromUTF8(m_controller.GetProblem().c_str()), "Marconi Server",
                         wxOK | wxICON_INFORMATION);
        }

        void TrayIcon::OnCheckAgain(wxCommandEvent&)
        {
            std::string error;
            if (!m_controller.CheckScreenAndInputAgain(error))
            {
                wxMessageBox(wxString::Format("The server could not start: %s", wxString::FromUTF8(error.c_str())),
                             "Marconi Server", wxOK | wxICON_ERROR);
                return;
            }
            if (!m_controller.GetProblem().empty())
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
            m_controller.Stop();
            RemoveIcon();
            wxTheApp->ExitMainLoop(); // no windows are open, so end the event loop directly; OnExit cleans up
        }
    } // namespace ServerUI
} // namespace Marconi
