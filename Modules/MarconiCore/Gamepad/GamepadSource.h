// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The game controllers plugged into this computer, as the viewer sees them: which are there, what state each is in,
// and a way to make one shake

#pragma once

#include <MarconiCore/Wire/WireGamepadMessages.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Gamepad
    {
        // What a controller is, as far as the other computer needs to know.
        struct PadInfo
        {
            Wire::GamepadType type = Wire::GamepadType::Generic;
            std::string name;
        };

        // Reads the controllers on this computer. The real ones are per platform (XInput on Windows, evdev on
        // Linux); tests use a fake. A source has a fixed number of slots; a controller sits in one slot for as long
        // as it is plugged in. Read is called from the viewer's thread; SetRumble may come from the connection's
        // thread, so it must be safe to call at the same time as Read.
        class GamepadSource
        {
        public:
            virtual ~GamepadSource() = default;

            // The number of slots, at most Wire::kMaxGamepads.
            virtual int GetSlotCount() const = 0;

            // Reads slot `slot`: false if no controller is plugged into it. Otherwise fills in `info` and the state
            // (in the protocol's layout: see WireGamepadMessages.h); `state.pad` and `state.sequence` are left alone.
            virtual bool Read(int slot, PadInfo& info, Wire::GamepadState& state) = 0;

            // Shakes the controller in `slot`: the heavy and light motor, 0 to 65535 each (0 and 0 stops it). Does
            // nothing for a slot with no controller or a controller that can't shake.
            virtual void SetRumble(int slot, uint16_t strong, uint16_t weak) = 0;
        };

        // The platform's source, or null where there is none (a phone, or a computer whose controller support isn't
        // written yet). `reason` then says why in plain words, for the menu.
        std::unique_ptr<GamepadSource> CreateSystemGamepadSource(std::string& reason);
    } // namespace Gamepad
} // namespace Marconi
