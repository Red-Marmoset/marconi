// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Locks this computer's screen, for the viewer's Lock message and the lock-on-disconnect setting

#pragma once

namespace Marconi
{
    namespace Server
    {
        // Locks the screen of the person signed in at this computer: LockWorkStation on Windows (the login screen
        // shows), `loginctl lock-session` or the screensaver's lock on Linux. False if there is no way to lock on
        // this platform or the call failed (macOS has none yet).
        bool LockThisComputer();
    } // namespace Server
} // namespace Marconi
