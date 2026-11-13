// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The session helper (`MarconiServer.exe --session <id>`): everything the server does for one console session. It
// reads the shared settings, makes or loads the server's identity, captures the screen and injects input, listens
// for viewers and answers the tray icon over the control pipe. The service starts one per console session as SYSTEM;
// on other platforms (and for tests) it can be run by hand.

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        struct SessionHostOptions
        {
            std::string dataDirectory;       // empty = GetDefaultDataDirectory()
            uint32_t consoleSession = 0;     // shown in the status; the helper is already running in it
            bool useTestPattern = false;     // serve a moving picture instead of the screen (manual checks)
            int portOverride = -1;           // >= 0 replaces the port from the settings file
            int testPatternWidth = 1280;
            int testPatternHeight = 720;
        };

        // Runs until SIGINT or SIGTERM (or the process is killed, which is how the service ends it). Returns the
        // process exit code: 0 after a signal, nonzero when it could not start (the reason is printed and logged),
        // which makes the service's supervisor wait and try again.
        int RunSessionHost(const SessionHostOptions& options);
    } // namespace Server
} // namespace Marconi
