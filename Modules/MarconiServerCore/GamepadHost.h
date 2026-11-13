// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's controllers on this computer: makes a virtual one for each the viewer connects, keeps its state, and
// removes them all when the viewer goes

#pragma once

#include "VirtualGamepad.h"

#include <MarconiCore/Wire/WireGamepadMessages.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace Marconi
{
    namespace Server
    {
        // Used by a Session for the controller messages. Methods are called from the session's input thread; rumble
        // comes back from the backend's own thread through `sendRumble`, which must be safe to call from any thread.
        class GamepadHost
        {
        public:
            using SendRumble = std::function<void(const Wire::GamepadRumble&)>;

            // `maxPads` is how many controllers one viewer may have at once (at most Wire::kMaxGamepads).
            GamepadHost(VirtualGamepadBackend& backend, SendRumble sendRumble,
                        uint8_t maxPads = Wire::kMaxGamepads);
            ~GamepadHost(); // releases everything still held, then removes every controller

            GamepadHost(const GamepadHost&) = delete;
            GamepadHost& operator=(const GamepadHost&) = delete;

            // A controller was plugged in at the viewer. Replaces one already in that slot. The answer goes back to
            // the viewer; Ok means the virtual controller exists.
            Wire::GamepadResult Connect(const Wire::GamepadConnect& message);

            // The controller's state. Ignored when the slot has no controller, or when `sequence` is not newer than
            // the last one applied (a datagram that arrived late or twice). True if it was applied and something
            // differed from the state before (which is what counts as the viewer being active).
            bool Update(const Wire::GamepadState& state);

            // The controller was unplugged: everything is released, then the virtual controller is removed.
            void Disconnect(uint8_t pad);

            // Disconnects every controller (the viewer left).
            void DisconnectAll();

            // How many virtual controllers exist now.
            int GetCount() const;

        private:
            struct Slot
            {
                std::unique_ptr<VirtualGamepad> pad;
                bool haveSequence = false;
                uint16_t lastSequence = 0;
                Wire::GamepadState lastState;
                std::string name;
            };

            void Release(Slot& slot, uint8_t index);

            VirtualGamepadBackend& m_backend;
            SendRumble m_sendRumble;
            uint8_t m_maxPads;
            mutable std::mutex m_mutex;
            Slot m_slots[Wire::kMaxGamepads];
        };
    } // namespace Server
} // namespace Marconi
