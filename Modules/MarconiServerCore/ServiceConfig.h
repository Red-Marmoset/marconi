// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The settings the service and its session helper share: a small text file in the server's all-users data folder
// (%ProgramData%\Marconi Server on Windows), next to the server's certificate and key. The installer and
// `--set-password` write the password hash into it; the helper reads it when it starts.

#pragma once

#include "Settings.h"

#include <string>

namespace Marconi
{
    namespace Server
    {
        // Where the shared settings live by default: %ProgramData%\Marconi Server on Windows, /var/lib/marconi-server
        // elsewhere. `--data-dir` overrides it (for tests and manual runs).
        std::string GetDefaultDataDirectory();

        // The file's path inside a data directory, and the certificate and key beside it.
        std::string GetConfigPath(const std::string& dataDirectory);
        std::string GetCertificatePath(const std::string& dataDirectory);
        std::string GetPrivateKeyPath(const std::string& dataDirectory);

        // Reads `settings` from the file: one `key=value` per line, `#` starts a comment. Keys: port, name,
        // password-hash, lock-on-disconnect (yes/no). Keys that are missing keep what `settings` already holds, so a
        // missing file is not an error (it leaves the defaults). False (with `error`) only when the file exists
        // and can't be read or has a value that makes no sense.
        bool LoadServiceConfig(const std::string& dataDirectory, Settings& settings, std::string& error);

        // Writes the file (making the folder if needed). Only the keys above are written: the certificate and key
        // have their own files.
        bool SaveServiceConfig(const std::string& dataDirectory, const Settings& settings, std::string& error);
    } // namespace Server
} // namespace Marconi
