// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server's saved settings

#include "ServerConfig.h"

#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>

namespace Marconi
{
    namespace ServerUI
    {
        namespace
        {
            const char* kConfigFile = "settings.ini";
            const char* kPortKey = "/Server/Port";
            const char* kPasswordHashKey = "/Server/PasswordHash";
            const char* kConfiguredKey = "/Server/Configured";
            const uint16_t kDefaultPort = 1895;

            // The file config the settings live in, inside the data folder.
            wxFileConfig MakeConfig(const wxString& dataDir)
            {
                wxFileName file(dataDir, kConfigFile);
                return wxFileConfig(wxEmptyString, wxEmptyString, file.GetFullPath(), wxEmptyString,
                                    wxCONFIG_USE_LOCAL_FILE);
            }
        } // namespace

        ServerConfig::ServerConfig()
        {
            // Ask for the XDG layout, as the client does, so on Linux the folder goes under the user's home rather
            // than a dot-file at the top of it. The app and vendor names (set in OnInit) decide the folder's name.
            wxStandardPaths::Get().SetFileLayout(wxStandardPaths::FileLayout_XDG);
            m_dataDir = wxStandardPaths::Get().GetUserLocalDataDir();
            if (!wxFileName::DirExists(m_dataDir))
            {
                if (!wxFileName::Mkdir(m_dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
                {
                    m_dataDir = wxEmptyString;
                }
            }
        }

        wxString ServerConfig::GetCertificatePath() const
        {
            return wxFileName(m_dataDir, "certificate.pem").GetFullPath();
        }

        wxString ServerConfig::GetKeyPath() const { return wxFileName(m_dataDir, "key.pem").GetFullPath(); }

        wxString ServerConfig::GetLogDirectory() const
        {
            wxFileName logs(m_dataDir, wxEmptyString);
            logs.AppendDir("logs");
            if (!logs.DirExists())
            {
                logs.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            }
            return logs.GetPath();
        }

        uint16_t ServerConfig::GetPort() const
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            long port = config.ReadLong(kPortKey, kDefaultPort);
            if (port < 1 || port > 65535)
            {
                return kDefaultPort;
            }
            return static_cast<uint16_t>(port);
        }

        void ServerConfig::SetPort(uint16_t port)
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            config.Write(kPortKey, static_cast<long>(port));
            config.Flush();
        }

        wxString ServerConfig::GetPasswordHash() const
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            return config.Read(kPasswordHashKey, wxEmptyString);
        }

        void ServerConfig::SetPasswordHash(const wxString& hash)
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            config.Write(kPasswordHashKey, hash);
            config.Write(kConfiguredKey, true);
            config.Flush();
        }

        bool ServerConfig::IsFirstRun() const
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            bool configured = false;
            config.Read(kConfiguredKey, &configured, false);
            return !configured;
        }

        void ServerConfig::MarkConfigured()
        {
            wxFileConfig config = MakeConfig(m_dataDir);
            config.Write(kConfiguredKey, true);
            config.Flush();
        }
    } // namespace ServerUI
} // namespace Marconi
