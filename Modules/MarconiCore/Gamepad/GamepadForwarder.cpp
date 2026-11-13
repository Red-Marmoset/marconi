// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Sends the viewer's game controllers to the other computer (see GamepadForwarder.h)

#include <MarconiCore/Gamepad/GamepadForwarder.h>

#include <algorithm>

namespace Marconi
{
    namespace Gamepad
    {
        GamepadForwarder::GamepadForwarder(GamepadSource& source, GamepadLink& link)
            : GamepadForwarder(source, link, Timing())
        {
        }

        GamepadForwarder::GamepadForwarder(GamepadSource& source, GamepadLink& link, Timing timing)
            : m_source(source), m_link(link), m_timing(timing)
        {
            m_link.SetGamepadListener(this);
        }

        GamepadForwarder::~GamepadForwarder()
        {
            m_link.SetGamepadListener(nullptr);
            Tick(std::chrono::steady_clock::now(), false); // let everything go
        }

        void GamepadForwarder::Release(int slot)
        {
            Slot& s = m_slots[slot];
            if (!s.connected)
            {
                return;
            }
            // Neutral first, then the disconnect: a server that misses either still ends up with nothing held.
            Wire::GamepadState neutral;
            neutral.pad = static_cast<uint8_t>(slot);
            neutral.sequence = ++s.sequence;
            m_link.SendGamepadState(neutral);
            Wire::GamepadDisconnect disconnect;
            disconnect.pad = static_cast<uint8_t>(slot);
            m_link.SendGamepadDisconnect(disconnect);
            s.connected = false;
            s.haveRumbleSequence = false;
            m_source.SetRumble(slot, 0, 0); // a controller left shaking would keep shaking in the person's hand
        }

        void GamepadForwarder::SendState(int slot, const Wire::GamepadState& state,
                                         std::chrono::steady_clock::time_point now)
        {
            Slot& s = m_slots[slot];
            Wire::GamepadState message = state;
            message.pad = static_cast<uint8_t>(slot);
            message.sequence = static_cast<uint16_t>(s.sequence + 1);
            if (m_link.SendGamepadState(message))
            {
                s.sequence = message.sequence;
                s.lastSent = message;
                s.lastSentAt = now;
            }
        }

        void GamepadForwarder::Tick(std::chrono::steady_clock::time_point now, bool active)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_link.SupportsGamepads())
            {
                active = false;
            }
            const int count = std::min<int>(m_source.GetSlotCount(), Wire::kMaxGamepads);
            for (int slot = 0; slot < count; ++slot)
            {
                Slot& s = m_slots[slot];
                if (!active)
                {
                    Release(slot);
                    s.refused = false;
                    continue;
                }
                PadInfo info;
                Wire::GamepadState state;
                if (!m_source.Read(slot, info, state))
                {
                    Release(slot);
                    s.refused = false;
                    continue;
                }
                if (s.refused)
                {
                    continue;
                }
                if (!s.connected)
                {
                    Wire::GamepadConnect connect;
                    connect.pad = static_cast<uint8_t>(slot);
                    connect.type = info.type;
                    connect.name = info.name.substr(0, Wire::kMaxGamepadNameBytes);
                    if (!m_link.SendGamepadConnect(connect))
                    {
                        continue; // try again on the next tick
                    }
                    s.connected = true;
                    s.sequence = 0;
                    SendState(slot, state, now); // the starting position, straight away
                    continue;
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.lastSentAt).count();
                const bool changed = !state.SameInputs(s.lastSent);
                if ((changed && elapsed >= m_timing.minIntervalMs) || elapsed >= m_timing.heartbeatMs)
                {
                    SendState(slot, state, now);
                }
            }
        }

        std::string GamepadForwarder::TakeNotice()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::string notice;
            notice.swap(m_notice);
            return notice;
        }

        int GamepadForwarder::GetConnectedCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            int count = 0;
            for (const Slot& s : m_slots)
            {
                if (s.connected)
                {
                    ++count;
                }
            }
            return count;
        }

        void GamepadForwarder::OnGamepadRumble(const Wire::GamepadRumble& rumble)
        {
            if (rumble.pad >= Wire::kMaxGamepads)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot& s = m_slots[rumble.pad];
            if (!s.connected)
            {
                return;
            }
            if (s.haveRumbleSequence && !Wire::IsNewerSequence(rumble.sequence, s.rumbleSequence))
            {
                return; // an older one that arrived late
            }
            s.haveRumbleSequence = true;
            s.rumbleSequence = rumble.sequence;
            m_source.SetRumble(rumble.pad, rumble.strong, rumble.weak);
        }

        void GamepadForwarder::OnGamepadResult(const Wire::GamepadResult& result)
        {
            if (result.status == Wire::GamepadResult::Status::Ok || result.pad >= Wire::kMaxGamepads)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot& s = m_slots[result.pad];
            s.refused = true;
            s.connected = false;
            if (m_notice.empty())
            {
                m_notice = result.message.empty() ? "The other computer wouldn't take the game controller."
                                                  : result.message;
            }
        }
    } // namespace Gamepad
} // namespace Marconi
