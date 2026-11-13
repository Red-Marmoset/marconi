// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Linux virtual controller's button and axis mapping, and the rumble bookkeeping (see UinputMapping.h)

#include "UinputMapping.h"

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            void AddEvent(std::vector<input_event>& events, uint16_t type, uint16_t code, int32_t value)
            {
                input_event event;
                std::memset(&event, 0, sizeof(event));
                event.type = type;
                event.code = code;
                event.value = value;
                events.push_back(event);
            }

            // The protocol's up-positive stick value as xpad reports it (down positive): the bitwise inverse, which
            // maps -32768 to 32767 without overflowing.
            int32_t FlipY(int16_t value)
            {
                return static_cast<int16_t>(~value);
            }

            int32_t Hat(uint16_t buttons, uint16_t negative, uint16_t positive)
            {
                return ((buttons & positive) != 0 ? 1 : 0) - ((buttons & negative) != 0 ? 1 : 0);
            }
        } // namespace

        const std::vector<UinputButton>& GetUinputButtons()
        {
            static const std::vector<UinputButton> buttons = {
                {Wire::GamepadButton::A, BTN_SOUTH},
                {Wire::GamepadButton::B, BTN_EAST},
                {Wire::GamepadButton::X, BTN_NORTH},
                {Wire::GamepadButton::Y, BTN_WEST},
                {Wire::GamepadButton::LeftShoulder, BTN_TL},
                {Wire::GamepadButton::RightShoulder, BTN_TR},
                {Wire::GamepadButton::Back, BTN_SELECT},
                {Wire::GamepadButton::Start, BTN_START},
                {Wire::GamepadButton::Guide, BTN_MODE},
                {Wire::GamepadButton::LeftStick, BTN_THUMBL},
                {Wire::GamepadButton::RightStick, BTN_THUMBR},
            };
            return buttons;
        }

        void BuildUinputEvents(const Wire::GamepadState& previous, const Wire::GamepadState& next, bool all,
                               std::vector<input_event>& events)
        {
            for (const UinputButton& button : GetUinputButtons())
            {
                const bool was = (previous.buttons & button.bit) != 0;
                const bool is = (next.buttons & button.bit) != 0;
                if (all || was != is)
                {
                    AddEvent(events, EV_KEY, button.code, is ? 1 : 0);
                }
            }
            const int32_t hatX = Hat(next.buttons, Wire::GamepadButton::DpadLeft, Wire::GamepadButton::DpadRight);
            const int32_t hatY = Hat(next.buttons, Wire::GamepadButton::DpadUp, Wire::GamepadButton::DpadDown);
            if (all || hatX != Hat(previous.buttons, Wire::GamepadButton::DpadLeft, Wire::GamepadButton::DpadRight))
            {
                AddEvent(events, EV_ABS, ABS_HAT0X, hatX);
            }
            if (all || hatY != Hat(previous.buttons, Wire::GamepadButton::DpadUp, Wire::GamepadButton::DpadDown))
            {
                AddEvent(events, EV_ABS, ABS_HAT0Y, hatY);
            }
            if (all || next.leftX != previous.leftX)
            {
                AddEvent(events, EV_ABS, ABS_X, next.leftX);
            }
            if (all || next.leftY != previous.leftY)
            {
                AddEvent(events, EV_ABS, ABS_Y, FlipY(next.leftY));
            }
            if (all || next.rightX != previous.rightX)
            {
                AddEvent(events, EV_ABS, ABS_RX, next.rightX);
            }
            if (all || next.rightY != previous.rightY)
            {
                AddEvent(events, EV_ABS, ABS_RY, FlipY(next.rightY));
            }
            if (all || next.leftTrigger != previous.leftTrigger)
            {
                AddEvent(events, EV_ABS, ABS_Z, next.leftTrigger);
            }
            if (all || next.rightTrigger != previous.rightTrigger)
            {
                AddEvent(events, EV_ABS, ABS_RZ, next.rightTrigger);
            }
            if (!events.empty())
            {
                AddEvent(events, EV_SYN, SYN_REPORT, 0);
            }
        }

        void RumbleTracker::Upload(int id, uint16_t strong, uint16_t weak, uint16_t lengthMs)
        {
            Effect& effect = m_effects[id];
            effect.strong = strong;
            effect.weak = weak;
            effect.lengthMs = lengthMs;
        }

        void RumbleTracker::Erase(int id)
        {
            m_effects.erase(id);
        }

        void RumbleTracker::Play(int id, int count, int64_t nowMs)
        {
            auto found = m_effects.find(id);
            if (found == m_effects.end())
            {
                return;
            }
            Effect& effect = found->second;
            effect.playing = count > 0;
            effect.endMs = (effect.playing && effect.lengthMs > 0) ? nowMs + static_cast<int64_t>(effect.lengthMs) * count
                                                                    : -1;
        }

        void RumbleTracker::Expire(int64_t nowMs)
        {
            for (auto& entry : m_effects)
            {
                Effect& effect = entry.second;
                if (effect.playing && effect.endMs >= 0 && nowMs >= effect.endMs)
                {
                    effect.playing = false;
                    effect.endMs = -1;
                }
            }
        }

        int64_t RumbleTracker::NextExpiry(int64_t nowMs) const
        {
            int64_t next = -1;
            for (const auto& entry : m_effects)
            {
                const Effect& effect = entry.second;
                if (effect.playing && effect.endMs >= 0)
                {
                    int64_t left = std::max<int64_t>(0, effect.endMs - nowMs);
                    next = next < 0 ? left : std::min(next, left);
                }
            }
            return next;
        }

        RumbleTracker::Result RumbleTracker::Current() const
        {
            uint32_t strong = 0;
            uint32_t weak = 0;
            for (const auto& entry : m_effects)
            {
                if (entry.second.playing)
                {
                    strong = std::max<uint32_t>(strong, entry.second.strong);
                    weak = std::max<uint32_t>(weak, entry.second.weak);
                }
            }
            Result result;
            result.strong = static_cast<uint16_t>(strong * m_gain / 0xFFFF);
            result.weak = static_cast<uint16_t>(weak * m_gain / 0xFFFF);
            return result;
        }
    } // namespace Server
} // namespace Marconi
