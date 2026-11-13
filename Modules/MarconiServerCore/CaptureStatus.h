// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the person at this computer needs to know about sharing its screen right now, when the answer isn't known
// at the moment the screen source is made (a Wayland desktop asks the person, and the server doesn't wait for them)

#pragma once

#include <functional>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Plain words for the tray and console, or empty when nothing needs saying. Cheap; safe from any thread.
        std::string GetCaptureStatus();

        // Set by a screen source that has such a state. Replaces any earlier provider.
        void SetCaptureStatusProvider(std::function<std::string()> provider);
    } // namespace Server
} // namespace Marconi
