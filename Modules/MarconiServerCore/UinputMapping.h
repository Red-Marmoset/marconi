// Copyright (c) 2026 Red Marmoset Studios Ltd.
// How a Marconi controller state becomes the events a Linux Xbox-style controller sends (and how the rumble a game
// asks for is worked out), kept apart from the device itself so it can be tested on any computer with the Linux
// headers. Only VirtualGamepad_Linux.cpp and the tests use it.

#pragma once

#include <MarconiCore/Wire/WireGamepadMessages.h>

#include <linux/input.h>

#include <cstdint>
#include <map>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // The USB identity of a Microsoft Xbox 360 controller, which is what makes games and SDL treat the virtual
        // controller as one (they choose the button layout from it).
        constexpr uint16_t kUinputVendor = 0x045e;
        constexpr uint16_t kUinputProduct = 0x028e;
        constexpr uint16_t kUinputVersion = 0x0110;
        constexpr char kUinputName[] = "Marconi virtual Xbox 360 controller";

        // One entry in the table of buttons: the protocol's bit and the evdev code the Xbox driver (xpad) sends.
        struct UinputButton
        {
            uint16_t bit;
            uint16_t code;
        };
        // The buttons a virtual controller has. The d-pad is not here: xpad reports it as the hat axes.
        const std::vector<UinputButton>& GetUinputButtons();

        // Builds the events that take the virtual controller from `previous` to `next`, ending with a sync event.
        // With `all` set every button and axis is written (the first state, so a game sees the starting position).
        // Y axes are turned over: the protocol has up positive, xpad has down positive.
        void BuildUinputEvents(const Wire::GamepadState& previous, const Wire::GamepadState& next, bool all,
                               std::vector<input_event>& events);

        // Follows the effects a game uploaded and plays, and says what the motors should be doing. Not thread safe.
        class RumbleTracker
        {
        public:
            struct Result
            {
                uint16_t strong = 0;
                uint16_t weak = 0;
                bool operator==(const Result& other) const { return strong == other.strong && weak == other.weak; }
            };

            // A game uploaded (or changed) an effect: how hard, and for how long in milliseconds (0 = until stopped).
            void Upload(int id, uint16_t strong, uint16_t weak, uint16_t lengthMs);
            void Erase(int id);
            // A game started the effect (`count` > 0, repeated that many times) or stopped it (`count` 0).
            void Play(int id, int count, int64_t nowMs);
            // The overall gain, 0 to 65535 (FF_GAIN); 65535 is full strength.
            void SetGain(uint16_t gain) { m_gain = gain; }
            // Effects that have run their length by `nowMs` stop.
            void Expire(int64_t nowMs);
            // How many milliseconds until the next effect ends, or -1 if none will.
            int64_t NextExpiry(int64_t nowMs) const;
            // What the motors should do now: the strongest of what is playing, scaled by the gain.
            Result Current() const;

        private:
            struct Effect
            {
                uint16_t strong = 0;
                uint16_t weak = 0;
                uint16_t lengthMs = 0;
                bool playing = false;
                int64_t endMs = -1; // when it stops by itself, or -1
            };
            std::map<int, Effect> m_effects;
            uint16_t m_gain = 0xFFFF;
        };
    } // namespace Server
} // namespace Marconi
