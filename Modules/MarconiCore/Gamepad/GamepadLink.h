// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the controller forwarder needs from a connection: a way to send the controller messages, and to hear back the
// server's answers and rumble. Wire::Client is the real one; tests use a fake.

#pragma once

#include <MarconiCore/Wire/WireGamepadMessages.h>

namespace Marconi
{
    namespace Gamepad
    {
        // Told about what the server sends back. Called on the connection's thread.
        class GamepadListener
        {
        public:
            virtual ~GamepadListener() = default;
            virtual void OnGamepadRumble(const Wire::GamepadRumble& rumble) = 0;
            virtual void OnGamepadResult(const Wire::GamepadResult& result) = 0;
        };

        class GamepadLink
        {
        public:
            virtual ~GamepadLink() = default;

            // True once connected to a server that agreed to take controllers.
            virtual bool SupportsGamepads() const = 0;
            // Each false if there is no connection (or the server doesn't take controllers) or the send failed.
            virtual bool SendGamepadConnect(const Wire::GamepadConnect& message) = 0;
            virtual bool SendGamepadDisconnect(const Wire::GamepadDisconnect& message) = 0;
            virtual bool SendGamepadState(const Wire::GamepadState& message) = 0;
            // Where the server's answers go (null = nowhere). Returns only once no call to the old listener is
            // running, so the caller may free it straight after.
            virtual void SetGamepadListener(GamepadListener* listener) = 0;
        };
    } // namespace Gamepad
} // namespace Marconi
