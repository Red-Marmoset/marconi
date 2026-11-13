// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Where the server keeps its settings and files, and reading and writing the port and the access password hash

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <wx/string.h>

namespace Marconi
{
    namespace ServerUI
    {
        // The server's saved settings and the folder they live in. Everything is kept under one data folder: a small
        // settings file, the TLS certificate and key, and the logs. On Windows that is ProgramData\Marconi Server when
        // the installer made it and this account can write in it (every account shares it, and the Windows service of
        // a later phase will use it); otherwise, and always on Linux and macOS, a per-user folder.
        // (Not yet run on Windows: the ProgramData choice is compiled only there.)
        class ServerConfig
        {
        public:
            ServerConfig();

            // The data folder, made if it wasn't there. Empty only if it couldn't be created.
            wxString GetDataDirectory() const { return m_dataDir; }
            wxString GetCertificatePath() const;
            wxString GetKeyPath() const;
            wxString GetLogDirectory() const;

            uint16_t GetPort() const;
            void SetPort(uint16_t port);

            // The access password as a stored hash (empty means no password is set). The plain password is never
            // kept; the caller hashes it first.
            wxString GetPasswordHash() const;
            void SetPasswordHash(const wxString& hash);

            // Whether viewers may browse and copy this computer's files. Off until someone turns it on.
            bool GetFilesEnabled() const;
            void SetFilesEnabled(bool enabled);

            // Who may connect: addresses and networks (Settings::allowedAddresses). Empty means anyone.
            std::vector<std::string> GetAllowedAddresses() const;
            void SetAllowedAddresses(const std::vector<std::string>& entries);

            // Minutes without any input after which a viewer is disconnected; 0 (the default) means never.
            int GetIdleMinutes() const;
            void SetIdleMinutes(int minutes);

            // Whether to show a notification here when a viewer connects or disconnects. On unless turned off.
            bool GetNotifyEnabled() const;
            void SetNotifyEnabled(bool enabled);

            // True until a password has been set (or deliberately left empty) once, so the tray app can prompt on
            // first run.
            bool IsFirstRun() const;
            void MarkConfigured();

        private:
            wxString m_dataDir;
        };
    } // namespace ServerUI
} // namespace Marconi
