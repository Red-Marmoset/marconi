// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The command-line modes the service setup needs: --service, --session, --set-password, --install-service and
// --remove-service. Each returns the process exit code.

#pragma once

#include <string>

namespace Marconi
{
    namespace Server
    {
        // --service: run as the Windows service (the Service Control Manager starts the program this way).
        int RunServiceMode();

        // --session <id>: the helper for one console session; see SessionHost.h.
        int RunSessionMode(const std::string& dataDirectory, unsigned consoleSession, bool useTestPattern, int port);

        // --set-password: reads one line from standard input (the password, never taken from the command line so
        // it doesn't show in process lists), hashes it and stores the hash in the settings file. Refuses an empty
        // password. Run it as an administrator: the settings folder is not writable by other users. A running
        // helper does not notice; the tray icon's Set password changes the live one.
        int RunSetPasswordMode(const std::string& dataDirectory);

        // --install-service [--no-start] and --remove-service.
        int RunInstallServiceMode(bool startNow);
        int RunRemoveServiceMode();
    } // namespace Server
} // namespace Marconi
