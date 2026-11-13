// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol's game controller messages: a controller joining and leaving, its state, and rumble back
//
// Built to travel on a datagram channel later: a state message carries the controller's WHOLE state and a
// sequence number, so a lost one is harmless (the next replaces it) and a late one is recognised and ignored.
// Everything is small and fixed-size apart from the name in GamepadConnect.
//
// The layout follows the Xbox controller, which is what every kind of controller is mapped onto. Buttons use the
// same bit values as Windows' XInput, so the Windows viewer passes them straight through and the Windows server's
// virtual Xbox controller takes them unchanged. Sticks run from -32768 to 32767 with up and right positive (also
// as XInput); triggers run from 0 to 255.

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Wire
    {
        // How many controllers one viewer can have connected at once (slots 0 to 3, as XInput has).
        constexpr uint8_t kMaxGamepads = 4;
        // The longest controller name a message may carry; a longer one is a malformed message.
        constexpr size_t kMaxGamepadNameBytes = 128;
        // Battery level: 0 (empty) to 100 (full), or this when the controller doesn't say or has a cable.
        constexpr uint8_t kBatteryUnknown = 0xFF;

        // What kind of controller the person is holding, so the other computer can show the right kind if it can.
        enum class GamepadType : uint8_t
        {
            Xbox = 0,        // Xbox 360, Xbox One and Xbox Series controllers, and anything that behaves like them
            PlayStation = 1, // DualShock 4 and DualSense
            Generic = 2      // any other game controller
        };

        // The bits of GamepadState::buttons (the values XInput uses; 0x0400 is the Guide, or Xbox, button).
        namespace GamepadButton
        {
            constexpr uint16_t DpadUp = 0x0001;
            constexpr uint16_t DpadDown = 0x0002;
            constexpr uint16_t DpadLeft = 0x0004;
            constexpr uint16_t DpadRight = 0x0008;
            constexpr uint16_t Start = 0x0010;
            constexpr uint16_t Back = 0x0020;
            constexpr uint16_t LeftStick = 0x0040;  // pressing the left stick in
            constexpr uint16_t RightStick = 0x0080; // pressing the right stick in
            constexpr uint16_t LeftShoulder = 0x0100;
            constexpr uint16_t RightShoulder = 0x0200;
            constexpr uint16_t Guide = 0x0400;
            constexpr uint16_t A = 0x1000;
            constexpr uint16_t B = 0x2000;
            constexpr uint16_t X = 0x4000;
            constexpr uint16_t Y = 0x8000;
            constexpr uint16_t All = DpadUp | DpadDown | DpadLeft | DpadRight | Start | Back | LeftStick |
                                     RightStick | LeftShoulder | RightShoulder | Guide | A | B | X | Y;
        } // namespace GamepadButton

        // True if `a` is later than `b` in a sequence that wraps around at 65536 (a is up to 32767 ahead of b).
        bool IsNewerSequence(uint16_t a, uint16_t b);

        // A controller was plugged in (or was there when forwarding started).
        struct GamepadConnect
        {
            uint8_t pad = 0; // the slot, 0 to kMaxGamepads - 1
            GamepadType type = GamepadType::Xbox;
            std::string name; // the controller's own name, for the other computer's log

            Bytes Encode() const;
            static GamepadConnect Decode(Reader& reader);
        };

        // That controller was unplugged (or forwarding stopped).
        struct GamepadDisconnect
        {
            uint8_t pad = 0;

            Bytes Encode() const;
            static GamepadDisconnect Decode(Reader& reader);
        };

        // The whole state of one controller at one moment.
        struct GamepadState
        {
            uint8_t pad = 0;
            uint16_t sequence = 0; // counts up by one for each state the viewer sends for this pad, wrapping around
            uint16_t buttons = 0;  // GamepadButton bits
            uint8_t leftTrigger = 0;
            uint8_t rightTrigger = 0;
            int16_t leftX = 0;
            int16_t leftY = 0;
            int16_t rightX = 0;
            int16_t rightY = 0;
            uint8_t battery = kBatteryUnknown;

            // True if everything but the pad number, the sequence and the battery is the same (what makes a state
            // worth sending).
            bool SameInputs(const GamepadState& other) const;
            // True if nothing is pressed or moved.
            bool IsNeutral() const;

            Bytes Encode() const;
            static GamepadState Decode(Reader& reader);
        };

        // The other computer wants the controller to shake. Each message is the whole picture (zero stops it), so a
        // lost one is harmless; `sequence` lets the viewer ignore one that arrives late.
        struct GamepadRumble
        {
            uint8_t pad = 0;
            uint16_t sequence = 0;
            uint16_t strong = 0; // the heavy motor, 0 to 65535
            uint16_t weak = 0;   // the light motor

            Bytes Encode() const;
            static GamepadRumble Decode(Reader& reader);
        };

        // The server's answer to a GamepadConnect.
        struct GamepadResult
        {
            enum class Status : uint8_t
            {
                Ok = 0,
                NoDriver = 1,     // the computer can't make a controller (a driver is missing, or it isn't supported)
                TooMany = 2,      // that many controllers are already connected
                NotSupported = 3, // this kind of computer has no way to do it yet
                Failed = 4        // it didn't work for another reason
            };
            uint8_t pad = 0;
            Status status = Status::Ok;
            std::string message; // a sentence for the person when not Ok

            Bytes Encode() const;
            static GamepadResult Decode(Reader& reader);
        };
    } // namespace Wire
} // namespace Marconi
