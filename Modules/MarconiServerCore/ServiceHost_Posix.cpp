// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows service has no counterpart on macOS and Linux yet (Phase 8 plans a launchd and a systemd service)

#include "ServiceHost.h"

namespace Marconi
{
    namespace Server
    {
        bool RunAsService(std::string& error)
        {
            error = "the service mode is for Windows only";
            return false;
        }

        bool InstallService(bool /*startNow*/, std::string& error)
        {
            error = "installing the service is for Windows only";
            return false;
        }

        bool RemoveService(std::string& error)
        {
            error = "removing the service is for Windows only";
            return false;
        }
    } // namespace Server
} // namespace Marconi
