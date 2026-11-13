// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Sends the viewer's game controllers to the other computer while that is switched on and the window is in front,
// and applies the shaking the other computer asks for

#pragma once

#include <MarconiCore/Gamepad/GamepadLink.h>
#include <MarconiCore/Gamepad/GamepadSource.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Gamepad
    {
        // The rules: a controller is announced when it is plugged in; its state goes out when it changes (but not
        // more often than `minIntervalMs` apart, so a stick sweeping at 1000 Hz can't flood the link) and, even
        // when nothing changed, every `heartbeatMs`, so the other computer recovers from a lost message and knows
        // the viewer is still there. When forwarding stops (switched off, window not in front, connection without
        // controller support) or a controller is unplugged, its state is sent neutral and it is disconnected, so
        // nothing is left held down over there.
        //
        // Tick is called on a timer from one thread (the viewer's UI thread). The listener callbacks come from the
        // connection's thread and only touch what the mutex guards and the source's SetRumble.
        class GamepadForwarder : public GamepadListener
        {
        public:
            struct Timing
            {
                int minIntervalMs = 8;    // at most about 125 states a second per controller
                int heartbeatMs = 250;    // a state at least this often while a controller is connected
            };

            GamepadForwarder(GamepadSource& source, GamepadLink& link);
            GamepadForwarder(GamepadSource& source, GamepadLink& link, Timing timing);
            ~GamepadForwarder() override; // stops forwarding (releasing everything) and unregisters from the link

            GamepadForwarder(const GamepadForwarder&) = delete;
            GamepadForwarder& operator=(const GamepadForwarder&) = delete;

            // Looks at the controllers. `active` says whether they should be forwarded right now (the setting is on
            // and the window is in front). Call about every 4 to 8 ms; it is cheap when nothing changed.
            void Tick(std::chrono::steady_clock::time_point now, bool active);

            // A sentence for the person when the other computer refused a controller ("Controllers need the ViGEmBus
            // driver..."), once; empty when there is none waiting.
            std::string TakeNotice();

            // How many controllers are connected to the other computer right now.
            int GetConnectedCount() const;

            // GamepadListener (the connection's thread).
            void OnGamepadRumble(const Wire::GamepadRumble& rumble) override;
            void OnGamepadResult(const Wire::GamepadResult& result) override;

        private:
            struct Slot
            {
                bool connected = false;
                bool refused = false; // the server said no; not asked again until the controller is plugged in anew
                uint16_t sequence = 0;
                Wire::GamepadState lastSent;
                std::chrono::steady_clock::time_point lastSentAt;
                bool haveRumbleSequence = false;
                uint16_t rumbleSequence = 0;
            };

            void Release(int slot);
            void SendState(int slot, const Wire::GamepadState& state, std::chrono::steady_clock::time_point now);

            GamepadSource& m_source;
            GamepadLink& m_link;
            Timing m_timing;
            mutable std::mutex m_mutex; // guards the slots' flags the connection thread also touches, and the notice
            Slot m_slots[Wire::kMaxGamepads];
            std::string m_notice;
        };
    } // namespace Gamepad
} // namespace Marconi
