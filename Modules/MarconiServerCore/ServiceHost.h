// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows service itself: the entry point the Service Control Manager runs, and installing and removing it.
// The decisions (which session, when to restart) are ServiceSupervisor's; this is the Windows glue around it.
// On other platforms every function says it is Windows only.

#pragma once

#include <string>

namespace Marconi
{
    namespace Server
    {
        // Runs as the Windows service ("MarconiServer"): connects to the Service Control Manager, then watches the
        // console session and keeps `<this exe> --session <id>` running in it as SYSTEM until the service is
        // stopped. Blocks until the service ends. False (with `error`) if it could not start as a service, for
        // example when run from a command prompt instead of by Windows.
        bool RunAsService(std::string& error);

        // Registers the service (LocalSystem, starts automatically, restarts itself if it crashes, runs this exe
        // with --service) and sets the "software SAS generation" policy so Ctrl+Alt+Del can be sent. Needs an
        // administrator. `startNow` also starts it. An existing service is updated in place.
        bool InstallService(bool startNow, std::string& error);

        // Stops the service if it is running, waits for it, deletes it and removes the policy value InstallService
        // set. Needs an administrator. Succeeds if the service was not installed.
        bool RemoveService(std::string& error);
    } // namespace Server
} // namespace Marconi
