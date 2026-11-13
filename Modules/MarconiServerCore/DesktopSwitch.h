// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Following the desktop that is receiving input, so UAC prompts, the lock screen and the sign-in
// screen are captured and can be typed into, and sending Ctrl+Alt+Del. The work is in DesktopSwitch_Windows.cpp;
// other platforms get DesktopSwitch_Posix.cpp, which does nothing.

#pragma once

#include <string>

namespace Marconi
{
    namespace Server
    {
        // Makes the calling thread use the desktop that currently receives input ("Default" normally, "Winlogon"
        // for the sign-in screen, the lock screen and UAC prompts). Capturing and injecting input both act on the
        // calling thread's desktop, so each such thread calls this before it grabs or injects. It re-checks at most
        // every 50 ms per thread. Returns true when it moved the thread to a different desktop since the last call
        // (the caller then re-gets any device context it holds), false when nothing changed or it couldn't switch.
        // Needs the SYSTEM account to reach the secure desktop; as an ordinary user it just stays on "Default".
        bool FollowInputDesktop();

        // The name of the desktop that receives input right now, for the tray icon's status; empty if unknown.
        std::string GetInputDesktopName();

        // Sends Ctrl+Alt+Del to the computer (the secure attention sequence, which no program can fake with
        // SendInput). The session helper asks the service to do it, because SendSAS only works from a service
        // unless a policy allows applications; if the service isn't there (a helper run by hand) it tries SendSAS
        // itself. False if neither worked.
        bool SendSecureAttention();
    } // namespace Server
} // namespace Marconi
