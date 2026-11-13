// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Game controllers on a Mac viewer: not written yet (it would use Apple's Game Controller framework). The viewer's menu
// says so in plain words. Not yet built on macOS.

#include <MarconiCore/Gamepad/GamepadSource.h>

namespace Marconi
{
    namespace Gamepad
    {
        std::unique_ptr<GamepadSource> CreateSystemGamepadSource(std::string& reason)
        {
            reason = "Sending game controllers from a Mac isn't available yet.";
            return nullptr;
        }
    } // namespace Gamepad
} // namespace Marconi
