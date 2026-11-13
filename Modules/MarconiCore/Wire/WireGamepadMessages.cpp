// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the game controller messages into bytes and back

#include <MarconiCore/Wire/WireGamepadMessages.h>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            uint8_t ReadPad(Reader& r)
            {
                uint8_t pad = r.U8();
                if (pad >= kMaxGamepads)
                {
                    throw WireError("controller slot out of range");
                }
                return pad;
            }

            uint16_t Pack(int16_t value)
            {
                return static_cast<uint16_t>(value);
            }
        } // namespace

        bool IsNewerSequence(uint16_t a, uint16_t b)
        {
            return a != b && static_cast<uint16_t>(a - b) < 0x8000;
        }

        Bytes GamepadConnect::Encode() const
        {
            return Build(Type::GamepadConnect,
                         [&](Writer& w) { w.U8(pad).U8(static_cast<uint8_t>(type)).String(name); });
        }

        GamepadConnect GamepadConnect::Decode(Reader& r)
        {
            GamepadConnect message;
            message.pad = ReadPad(r);
            uint8_t type = r.U8();
            message.type = type <= static_cast<uint8_t>(GamepadType::Generic) ? static_cast<GamepadType>(type)
                                                                                : GamepadType::Generic;
            message.name = r.String();
            if (message.name.size() > kMaxGamepadNameBytes)
            {
                throw WireError("controller name too long");
            }
            return message;
        }

        Bytes GamepadDisconnect::Encode() const
        {
            return Build(Type::GamepadDisconnect, [&](Writer& w) { w.U8(pad); });
        }

        GamepadDisconnect GamepadDisconnect::Decode(Reader& r)
        {
            GamepadDisconnect message;
            message.pad = ReadPad(r);
            return message;
        }

        bool GamepadState::SameInputs(const GamepadState& other) const
        {
            return buttons == other.buttons && leftTrigger == other.leftTrigger &&
                   rightTrigger == other.rightTrigger && leftX == other.leftX && leftY == other.leftY &&
                   rightX == other.rightX && rightY == other.rightY;
        }

        bool GamepadState::IsNeutral() const
        {
            return buttons == 0 && leftTrigger == 0 && rightTrigger == 0 && leftX == 0 && leftY == 0 && rightX == 0 &&
                   rightY == 0;
        }

        Bytes GamepadState::Encode() const
        {
            return Build(Type::GamepadState, [&](Writer& w) {
                w.U8(pad).U16(sequence).U16(buttons).U8(leftTrigger).U8(rightTrigger);
                w.U16(Pack(leftX)).U16(Pack(leftY)).U16(Pack(rightX)).U16(Pack(rightY)).U8(battery);
            });
        }

        GamepadState GamepadState::Decode(Reader& r)
        {
            GamepadState message;
            message.pad = ReadPad(r);
            message.sequence = r.U16();
            // Bits that aren't buttons are dropped, so a hostile viewer can't set one the server never defined.
            message.buttons = static_cast<uint16_t>(r.U16() & GamepadButton::All);
            message.leftTrigger = r.U8();
            message.rightTrigger = r.U8();
            message.leftX = static_cast<int16_t>(r.U16());
            message.leftY = static_cast<int16_t>(r.U16());
            message.rightX = static_cast<int16_t>(r.U16());
            message.rightY = static_cast<int16_t>(r.U16());
            message.battery = r.U8();
            if (message.battery > 100 && message.battery != kBatteryUnknown)
            {
                message.battery = kBatteryUnknown;
            }
            return message;
        }

        Bytes GamepadRumble::Encode() const
        {
            return Build(Type::GamepadRumble, [&](Writer& w) { w.U8(pad).U16(sequence).U16(strong).U16(weak); });
        }

        GamepadRumble GamepadRumble::Decode(Reader& r)
        {
            GamepadRumble message;
            message.pad = ReadPad(r);
            message.sequence = r.U16();
            message.strong = r.U16();
            message.weak = r.U16();
            return message;
        }

        Bytes GamepadResult::Encode() const
        {
            return Build(Type::GamepadResult,
                         [&](Writer& w) { w.U8(pad).U8(static_cast<uint8_t>(status)).String(message); });
        }

        GamepadResult GamepadResult::Decode(Reader& r)
        {
            GamepadResult message;
            message.pad = ReadPad(r);
            uint8_t status = r.U8();
            if (status > static_cast<uint8_t>(Status::Failed))
            {
                throw WireError("unknown controller result");
            }
            message.status = static_cast<Status>(status);
            message.message = r.String();
            if (message.message.size() > 1024)
            {
                throw WireError("controller message too long");
            }
            return message;
        }
    } // namespace Wire
} // namespace Marconi
