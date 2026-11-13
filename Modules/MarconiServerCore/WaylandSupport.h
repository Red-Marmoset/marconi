// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: choosing and sharing the Wayland desktop-portal session between the screen source and the input sink

#pragma once

#include "InputSink.h"
#include "ScreenSource.h"
#include "WaylandPortal.h"

#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // True when the screen and input should come from the desktop portal: the person is in a Wayland session
        // (XDG_SESSION_TYPE=wayland, or WAYLAND_DISPLAY with no DISPLAY), or MARCONI_WAYLAND_PORTAL=1 forces it (for
        // tests and odd setups). MARCONI_WAYLAND_PORTAL=0 turns it off.
        bool ShouldUseWaylandPortal();

        // The one portal session of this program, started on first use and shared by the screen and the input (one
        // question to the person covers both). It ends when the last user lets go, so a later call asks afresh.
        std::shared_ptr<WaylandPortalSession> AcquireWaylandPortal();

        // Replaces the options the next AcquireWaylandPortal starts with (the bus address and where the "remember my
        // answer" token is kept). For tests.
        void SetWaylandPortalOptions(const WaylandPortalOptions& options);

        std::unique_ptr<ScreenSource> CreateWaylandScreen(std::shared_ptr<WaylandPortalSession> session);
        std::unique_ptr<InputSink> CreateWaylandInput(std::shared_ptr<WaylandPortalSession> session);

        // The factory branches (called from SystemScreen_Linux.cpp and SystemInput_Linux.cpp when the session is
        // Wayland). They return at once or after a short wait; the person's answer to the desktop's question comes
        // later and is reported by GetCaptureStatus (CaptureStatus.h). Null only when D-Bus can't be loaded at all.
        std::unique_ptr<ScreenSource> CreateWaylandSystemScreen(std::string& error);
        std::unique_ptr<InputSink> CreateWaylandSystemInput(std::string& error);
    } // namespace Server
} // namespace Marconi
