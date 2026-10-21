// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Starts the server, puts an icon in the system tray, and keeps running until Quit

#include "ServerApp.h"

#include "ServerConfig.h"
#include "ServerController.h"
#include "TrayIcon.h"

#include <MarconiCore/Version.h>
#include <MarconiServerCore/IdentityStore.h>
#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/Settings.h>

#include <Log/Log.h>

#include <wx/bmpbndl.h>
#include <wx/dcmemory.h>
#include <wx/icon.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace Marconi
{
    namespace ServerUI
    {
        namespace
        {
            // A small drawn icon so the tray app needs no image file: a rounded square in Marconi's blue with a
            // white "M".
            wxIcon MakeTrayIcon()
            {
                const int size = 32;
                wxBitmap bitmap(size, size, 32);
                {
                    wxMemoryDC dc(bitmap);
                    dc.SetBackground(*wxTRANSPARENT_BRUSH);
                    dc.Clear();
                    dc.SetBrush(wxBrush(wxColour(0x1E, 0x66, 0xD0)));
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawRoundedRectangle(0, 0, size, size, 6);
                    wxFont font(wxFontInfo(16).Bold());
                    dc.SetFont(font);
                    dc.SetTextForeground(*wxWHITE);
                    wxSize text = dc.GetTextExtent("M");
                    dc.DrawText("M", (size - text.GetWidth()) / 2, (size - text.GetHeight()) / 2);
                }
                wxIcon icon;
                icon.CopyFromBitmap(bitmap);
                return icon;
            }

            // Asks for an access password (empty allowed = no password). Returns false if the person cancelled.
            // On OK, `hashOut` is the stored hash (empty for no password).
            bool AskForPassword(const wxString& message, std::string& hashOut)
            {
                wxPasswordEntryDialog dialog(nullptr, message, "Marconi Server password");
                if (dialog.ShowModal() != wxID_OK)
                {
                    return false;
                }
                wxString password = dialog.GetValue();
                if (password.IsEmpty())
                {
                    hashOut.clear();
                    return true;
                }
                hashOut = Server::HashPassword(password.utf8_string());
                return true;
            }
        } // namespace

        bool ServerApp::OnInit()
        {
            // A plain lowercase name for the data folder and config; the window and menu text say "Marconi Server".
            SetAppName("marconi-server");
            SetVendorName("Red Marmoset Studios");

            m_config = std::make_unique<ServerConfig>();
            if (m_config->GetDataDirectory().IsEmpty())
            {
                wxMessageBox("Could not create the folder for Marconi Server's settings.", "Marconi Server",
                             wxOK | wxICON_ERROR);
                return false;
            }

            Log::InitializeFileLogging(m_config->GetLogDirectory().utf8_string(), "MarconiServer");
            LOG_INFO("Server", "Marconi Server %s starting", GetVersionString());

            // Build the settings from what's saved, and make or load the lasting TLS identity so viewers aren't asked
            // to trust the server again after every restart.
            Server::Settings settings;
            settings.serverVersion = GetVersionString();
            settings.serverName = wxGetHostName().utf8_string();
            settings.port = m_config->GetPort();
            settings.passwordHash = m_config->GetPasswordHash().utf8_string();

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

            // First run: ask for a password once (empty is allowed, and means anyone who reaches the port can
            // connect). After this the tray menu changes it.
            if (m_config->IsFirstRun())
            {
                std::string hash;
                if (AskForPassword("Welcome to Marconi Server.\n\nSet an access password, or leave it empty for no "
                                   "password. Viewers are asked for it when they connect.",
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
            std::string startError;
            if (!m_controller->Start(startError))
            {
                wxMessageBox(wxString::Format("The server could not start on port %u: %s\n\nUse the tray menu to "
                                              "choose another port.",
                                              static_cast<unsigned>(settings.port),
                                              wxString::FromUTF8(startError.c_str())),
                             "Marconi Server", wxOK | wxICON_WARNING);
            }

            m_tray = new TrayIcon(*m_controller, *m_config);
            if (!m_tray->SetIcon(MakeTrayIcon(), "Marconi Server"))
            {
                LOG_WARNING("Server", "Could not put the icon in the system tray");
            }

            // There is no window; the tray icon keeps the program running until Quit.
            SetExitOnFrameDelete(false);
            return true;
        }

        int ServerApp::OnExit()
        {
            if (m_controller)
            {
                m_controller->Stop();
            }
            if (m_tray)
            {
                m_tray->RemoveIcon();
                m_tray->Destroy();
                m_tray = nullptr;
            }
            LOG_INFO("Server", "Marconi Server stopped");
            Log::ShutdownFileLogging();
            return wxApp::OnExit();
        }
    } // namespace ServerUI
} // namespace Marconi

wxIMPLEMENT_APP(Marconi::ServerUI::ServerApp);
