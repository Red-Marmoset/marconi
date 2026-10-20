// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon and its menu

#include "TrayIcon.h"

#include "ServerConfig.h"
#include "ServerController.h"

#include <MarconiServerCore/PasswordHash.h>

#include <Log/Log.h>

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/textdlg.h>

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
                ID_CopyFingerprint,
            };
        } // namespace

        wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
            EVT_MENU(ID_SetPassword, TrayIcon::OnSetPassword)
            EVT_MENU(ID_ChangePort, TrayIcon::OnChangePort)
            EVT_MENU(ID_CopyFingerprint, TrayIcon::OnCopyFingerprint)
            EVT_MENU(wxID_EXIT, TrayIcon::OnQuit)
        wxEND_EVENT_TABLE()

        TrayIcon::TrayIcon(ServerController& controller, ServerConfig& config)
            : m_controller(controller), m_config(config)
        {
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
            menu->Append(wxID_ANY, hasPassword ? "Password required" : "No password set")->Enable(false);

            wxString fingerprint = wxString::FromUTF8(m_controller.GetFingerprint().c_str());
            if (!fingerprint.IsEmpty())
            {
                wxString shortPrint = fingerprint.Left(16) + "…"; // first 16 hex digits then an ellipsis
                menu->Append(wxID_ANY, "Fingerprint " + shortPrint)->Enable(false);
                menu->Append(ID_CopyFingerprint, "Copy full fingerprint");
            }

            menu->AppendSeparator();
            menu->Append(ID_SetPassword, hasPassword ? "Change password…" : "Set password…");
            menu->Append(ID_ChangePort, "Change port…");
            menu->AppendSeparator();
            menu->Append(wxID_EXIT, "Quit Marconi Server");
            return menu;
        }

        void TrayIcon::OnSetPassword(wxCommandEvent&)
        {
            wxPasswordEntryDialog dialog(nullptr,
                                         "Enter an access password for this server, or leave it empty for no "
                                         "password.\nViewers are asked for it when they connect.",
                                         "Marconi Server password");
            if (dialog.ShowModal() != wxID_OK)
            {
                return;
            }
            wxString password = dialog.GetValue();

            std::string hash;
            if (!password.IsEmpty())
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

        void TrayIcon::OnQuit(wxCommandEvent&)
        {
            m_controller.Stop();
            RemoveIcon();
            wxTheApp->ExitMainLoop(); // no windows are open, so end the event loop directly; OnExit cleans up
        }
    } // namespace ServerUI
} // namespace Marconi
