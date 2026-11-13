// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Locks the Windows workstation with LockWorkStation.
//
// NOTE: not yet built or run on Windows (the cloud build is Linux). LockWorkStation locks the interactive session
// of the calling process, so it works from the session helper, which runs in the console session; it would not
// from the service in session 0.

#include "SystemLock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Marconi
{
    namespace Server
    {
        bool LockThisComputer()
        {
            return LockWorkStation() != 0;
        }
    } // namespace Server
} // namespace Marconi
