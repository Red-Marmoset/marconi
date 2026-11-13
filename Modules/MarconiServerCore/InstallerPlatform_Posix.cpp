// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Self-update on Linux and macOS: not supported, so there is no inspector and no launcher
//
// A Linux server is installed from a .deb package, which only the administrator (root) can install, and a Mac server
// is an app bundle; neither has a silent, permission-free way to replace itself while a viewer watches. The viewer
// is told "can't update itself on that system" (SERVER_PLAN.md, "Self-update").

#include "UpdateHost.h"

namespace Marconi
{
    namespace Server
    {
        std::unique_ptr<InstallerInspector> CreatePlatformInstallerInspector()
        {
            return nullptr;
        }

        std::unique_ptr<InstallerLauncher> CreatePlatformInstallerLauncher()
        {
            return nullptr;
        }
    } // namespace Server
} // namespace Marconi
