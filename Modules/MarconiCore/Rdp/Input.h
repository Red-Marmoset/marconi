// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP input: keyboard (scancodes) and mouse events, wrapped in a TS_INPUT_PDU (MS-RDPBCGR 2.2.8.1.1.3). The viewer
// speaks X11 keysyms (as VNC does), so a keysym is mapped to a set-1 scancode here. RDP milestone 5.

#pragma once

#include <MarconiCore/Rdp/Pdu.h>

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // A set-1 keyboard scancode and whether it is an "extended" key (the E0 group: right Ctrl/Alt, arrows, etc).
        struct Scancode
        {
            uint16_t code = 0;
            bool extended = false;
            bool known = false; // false when the keysym has no scancode here
        };

        // Maps an X11 keysym (the viewer's key events) to a set-1 scancode. A printable Latin character maps by its
        // position on a US keyboard; function, navigation and modifier keys map by name.
        Scancode KeysymToScancode(uint32_t keysym);

        // A scancode input event: key down or up, from a Scancode.
        Bytes ScancodeEvent(const Scancode& scancode, bool down);
        // A Unicode input event (for characters with no scancode): the UTF-16 code unit.
        Bytes UnicodeEvent(uint16_t codeUnit, bool down);
        // A mouse event: pointerFlags (PTRFLAGS_*), and the pointer position in desktop pixels.
        Bytes MouseEvent(uint16_t pointerFlags, uint16_t x, uint16_t y);

        // Translates a VNC-style button mask (bit 0 left, 1 middle, 2 right, 3 wheel up, 4 wheel down) and the
        // pointer position into the RDP mouse events that take the pointer from `previousMask` to `mask`: always a
        // move, then a down or up for each of the three buttons that changed, then one wheel notch for each wheel
        // bit newly set (the wheel bits are momentary, acted on only when they go from clear to set).
        std::vector<Bytes> PointerEvents(uint8_t mask, uint8_t previousMask, uint16_t x, uint16_t y);

        // Pointer flags (MS-RDPBCGR 2.2.8.1.1.3.1.1.3).
        enum : uint16_t
        {
            kPtrMove = 0x0800,
            kPtrDown = 0x8000,
            kPtrButton1 = 0x1000, // left
            kPtrButton2 = 0x2000, // right
            kPtrButton3 = 0x4000, // middle
            kPtrWheel = 0x0200,
            kPtrWheelNegative = 0x0100,
        };

        // Wraps one or more input events (ScancodeEvent / MouseEvent / UnicodeEvent outputs) in a TS_INPUT_PDU,
        // ready for a Send Data Request on the I/O channel. `shareId` and `source` as elsewhere.
        Bytes BuildInputPdu(uint32_t shareId, uint16_t source, const std::vector<Bytes>& events);
    } // namespace Rdp
} // namespace Marconi
