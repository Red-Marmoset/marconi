// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A game controller the server makes up on this computer, so a game here sees the pad the viewer is holding
//
// This is the seam between the protocol and the operating system. Everything that knows about a driver lives
// behind these two interfaces, one file per platform (VirtualGamepad_Linux.cpp, _Windows.cpp, _Mac.cpp), so the
// Windows driver can be swapped for another (a fork of ViGEmBus, or our own) by replacing one file.

#pragma once

#include <MarconiCore/Wire/WireGamepadMessages.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Called when a game asks the controller to shake: the strength of the heavy and light motor, 0 to 65535
        // each (0 and 0 stops it). It may be called from any thread, until the controller is destroyed.
        using RumbleHandler = std::function<void(uint16_t strong, uint16_t weak)>;

        // One virtual controller. It exists from Create until it is destroyed; destroying it removes it from the
        // computer, and no RumbleHandler call is running or starts once the destructor has returned.
        class VirtualGamepad
        {
        public:
            virtual ~VirtualGamepad() = default;

            // Sets the controller's whole state (`pad` and `sequence` are ignored). False if the computer refused.
            virtual bool Update(const Wire::GamepadState& state) = 0;
        };

        // Makes virtual controllers, or says why it can't.
        class VirtualGamepadBackend
        {
        public:
            virtual ~VirtualGamepadBackend() = default;

            // A short name for what makes the controllers ("uinput", "ViGEmBus"), for the log.
            virtual std::string GetName() const = 0;

            // Makes a controller of this kind (a backend with only one kind makes that and treats the type as a
            // hint). On failure returns null, sets `status` (never Ok) and `message`, a sentence a person can act on.
            virtual std::unique_ptr<VirtualGamepad> Create(Wire::GamepadType type, RumbleHandler onRumble,
                                                           Wire::GamepadResult::Status& status,
                                                           std::string& message) = 0;
        };

        // The platform's backend: uinput on Linux, ViGEmBus on Windows, and on macOS one that says controllers
        // aren't available yet. Never null: when the computer can't do it, Create fails with the reason, so the
        // viewer is told in plain words.
        std::unique_ptr<VirtualGamepadBackend> CreateSystemGamepadBackend();
    } // namespace Server
} // namespace Marconi
