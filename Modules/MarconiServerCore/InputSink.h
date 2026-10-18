// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Where the viewer's keyboard and mouse go: injected into the computer the server runs on

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Applies the viewer's input to this computer. The real one uses the platform (SendInput on Windows, and
        // so on); tests record what arrived. Called from the session's input thread only.
        class InputSink
        {
        public:
            virtual ~InputSink() = default;

            // The pointer, in whole-desktop pixels. `buttons` is a bit mask (bit 0 left, 1 middle, 2 right); the
            // high-resolution wheel deltas are 120 per notch, 0 for none.
            virtual void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) = 0;

            // A key by meaning (an X11 keysym) and, when known, by physical key (a USB HID usage, 0 if not), down
            // or up. The implementation prefers the scancode for games and falls back to the keysym.
            virtual void SendKey(uint32_t keysym, uint32_t scancode, bool down) = 0;

            // A run of characters to type that have no plain key (accents, other scripts).
            virtual void TypeText(const std::string& utf8) = 0;
        };
    } // namespace Server
} // namespace Marconi
