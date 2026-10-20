// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Where the server keeps its settings and files, and reading and writing the port and the access password hash

#pragma once

#include <cstdint>

#include <wx/string.h>

namespace Marconi
{
    namespace ServerUI
    {
        // The server's saved settings and the folder they live in. Everything is kept under one per-user data folder
        // ("Marconi Server"): a small settings file, the TLS certificate and key, and the logs. (An all-users
        // location and a Windows service come with a later phase; for now the server runs as the signed-in user.)
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

            // True until a password has been set (or deliberately left empty) once, so the tray app can prompt on
            // first run.
            bool IsFirstRun() const;
            void MarkConfigured();

        private:
            wxString m_dataDir;
        };
    } // namespace ServerUI
} // namespace Marconi
