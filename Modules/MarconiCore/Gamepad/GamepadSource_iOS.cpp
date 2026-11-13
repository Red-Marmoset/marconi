// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Game controllers on iOS: not available in the app yet.

#include <MarconiCore/Gamepad/GamepadSource.h>

namespace Marconi
{
    namespace Gamepad
    {
        std::unique_ptr<GamepadSource> CreateSystemGamepadSource(std::string& reason)
        {
            reason = "Sending game controllers from this app isn't available yet.";
            return nullptr;
        }
    } // namespace Gamepad
} // namespace Marconi
