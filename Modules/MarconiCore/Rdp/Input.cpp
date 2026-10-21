// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP input events and the keysym-to-scancode mapping.

#include <MarconiCore/Rdp/Input.h>

#include <MarconiCore/Keysyms.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            }
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }

            // Input event message types (MS-RDPBCGR 2.2.8.1.1.3.1.1).
            enum : uint16_t
            {
                kEventScancode = 0x0004,
                kEventUnicode = 0x0005,
                kEventMouse = 0x8001,
            };
            enum : uint16_t
            {
                kKbdRelease = 0x8000,
                kKbdExtended = 0x0100,
            };

            Scancode Simple(uint16_t code) { return {code, false, true}; }
            Scancode Extended(uint16_t code) { return {code, true, true}; }
        } // namespace

        Scancode KeysymToScancode(uint32_t keysym)
        {
            using namespace Keysym;
            // Letters: by US-keyboard position, case-insensitive (Shift is sent separately).
            if ((keysym >= 'a' && keysym <= 'z') || (keysym >= 'A' && keysym <= 'Z'))
            {
                static const uint16_t letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
                                                     0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
                                                     0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
                uint32_t lower = (keysym >= 'A' && keysym <= 'Z') ? keysym + 32 : keysym;
                return Simple(letters[lower - 'a']);
            }
            // Top-row digits and their shifted symbols share scancodes.
            switch (keysym)
            {
                case '1': case '!': return Simple(0x02);
                case '2': case '@': return Simple(0x03);
                case '3': case '#': return Simple(0x04);
                case '4': case '$': return Simple(0x05);
                case '5': case '%': return Simple(0x06);
                case '6': case '^': return Simple(0x07);
                case '7': case '&': return Simple(0x08);
                case '8': case '*': return Simple(0x09);
                case '9': case '(': return Simple(0x0A);
                case '0': case ')': return Simple(0x0B);
                case '-': case '_': return Simple(0x0C);
                case '=': case '+': return Simple(0x0D);
                case '`': case '~': return Simple(0x29);
                case '[': case '{': return Simple(0x1A);
                case ']': case '}': return Simple(0x1B);
                case '\\': case '|': return Simple(0x2B);
                case ';': case ':': return Simple(0x27);
                case '\'': case '"': return Simple(0x28);
                case ',': case '<': return Simple(0x33);
                case '.': case '>': return Simple(0x34);
                case '/': case '?': return Simple(0x35);
                case ' ': return Simple(0x39);
                default: break;
            }
            switch (keysym)
            {
                case Escape: return Simple(0x01);
                case BackSpace: return Simple(0x0E);
                case Tab: return Simple(0x0F);
                case Return: return Simple(0x1C);
                case Control_L: return Simple(0x1D);
                case Control_R: return Extended(0x1D);
                case Shift_L: return Simple(0x2A);
                case Shift_R: return Simple(0x36);
                case Alt_L: return Simple(0x38);
                case Alt_R: case ISO_Level3_Shift: return Extended(0x38);
                case Super_L: return Extended(0x5B);
                case Super_R: return Extended(0x5C);
                case Menu: return Extended(0x5D);
                case CapsLock: return Simple(0x3A);
                case NumLock: return Simple(0x45);
                case ScrollLock: return Simple(0x46);
                case Delete: return Extended(0x53);
                case Insert: return Extended(0x52);
                case Home: return Extended(0x47);
                case End: return Extended(0x4F);
                case PageUp: return Extended(0x49);
                case PageDown: return Extended(0x51);
                case Up: return Extended(0x48);
                case Down: return Extended(0x50);
                case Left: return Extended(0x4B);
                case Right: return Extended(0x4D);
                case Print: return Extended(0x37);
                case Pause: return Simple(0x45);
                default: break;
            }
            if (keysym >= F1 && keysym <= F1 + 11) // F1..F12
            {
                static const uint16_t fkeys[12] = {0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
                                                   0x41, 0x42, 0x43, 0x44, 0x57, 0x58};
                return Simple(fkeys[keysym - F1]);
            }
            return {0, false, false};
        }

        Bytes ScancodeEvent(const Scancode& scancode, bool down)
        {
            Bytes out;
            PutU32(out, 0); // eventTime (ignored by servers)
            PutU16(out, kEventScancode);
            uint16_t flags = (down ? 0 : kKbdRelease) | (scancode.extended ? kKbdExtended : 0);
            PutU16(out, flags);
            PutU16(out, scancode.code);
            PutU16(out, 0); // pad
            return out;
        }

        Bytes UnicodeEvent(uint16_t codeUnit, bool down)
        {
            Bytes out;
            PutU32(out, 0);
            PutU16(out, kEventUnicode);
            PutU16(out, down ? 0 : kKbdRelease);
            PutU16(out, codeUnit);
            PutU16(out, 0);
            return out;
        }

        Bytes MouseEvent(uint16_t pointerFlags, uint16_t x, uint16_t y)
        {
            Bytes out;
            PutU32(out, 0);
            PutU16(out, kEventMouse);
            PutU16(out, pointerFlags);
            PutU16(out, x);
            PutU16(out, y);
            return out;
        }

        Bytes BuildInputPdu(uint32_t shareId, uint16_t source, const std::vector<Bytes>& events)
        {
            Bytes body;
            PutU16(body, static_cast<uint16_t>(events.size())); // numberEvents
            PutU16(body, 0);                                    // pad
            for (const Bytes& event : events)
            {
                body.insert(body.end(), event.begin(), event.end());
            }
            return WrapShareData(shareId, source, ShareDataType::Input, body);
        }
    } // namespace Rdp
} // namespace Marconi
