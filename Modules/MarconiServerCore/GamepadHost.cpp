// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's controllers on this computer

#include "GamepadHost.h"

#include <Log/Log.h>

#include <algorithm>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // A controller's name as it may go in the log: it came from the viewer, so control characters (a line
            // break could forge a log line) become spaces and a long name is cut.
            std::string LogSafe(const std::string& text)
            {
                std::string safe = text.substr(0, 64);
                for (char& c : safe)
                {
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                    {
                        c = ' ';
                    }
                }
                return safe;
            }
        } // namespace

        GamepadHost::GamepadHost(VirtualGamepadBackend& backend, SendRumble sendRumble, uint8_t maxPads)
            : m_backend(backend), m_sendRumble(std::move(sendRumble)),
              m_maxPads(std::min<uint8_t>(maxPads, Wire::kMaxGamepads))
        {
        }

        GamepadHost::~GamepadHost()
        {
            DisconnectAll();
        }

        Wire::GamepadResult GamepadHost::Connect(const Wire::GamepadConnect& message)
        {
            Wire::GamepadResult result;
            result.pad = message.pad;
            if (message.pad >= m_maxPads)
            {
                result.status = Wire::GamepadResult::Status::TooMany;
                result.message = "The other computer allows at most " + std::to_string(m_maxPads) +
                                 " game controllers at once.";
                return result;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            Slot& slot = m_slots[message.pad];
            if (slot.pad != nullptr)
            {
                Release(slot, message.pad); // plugged in again: start from a clean controller
            }

            // Rumble goes back to the viewer with a count of its own, so one that arrives late is recognised there.
            auto counter = std::make_shared<std::atomic<uint16_t>>(0);
            const uint8_t index = message.pad;
            SendRumble send = m_sendRumble;
            RumbleHandler onRumble = [send, counter, index](uint16_t strong, uint16_t weak) {
                Wire::GamepadRumble rumble;
                rumble.pad = index;
                rumble.sequence = static_cast<uint16_t>(++*counter);
                rumble.strong = strong;
                rumble.weak = weak;
                if (send)
                {
                    send(rumble);
                }
            };

            std::string error;
            Wire::GamepadResult::Status status = Wire::GamepadResult::Status::Ok;
            std::unique_ptr<VirtualGamepad> pad = m_backend.Create(message.type, std::move(onRumble), status, error);
            if (pad == nullptr)
            {
                result.status = status == Wire::GamepadResult::Status::Ok ? Wire::GamepadResult::Status::Failed : status;
                result.message = error;
                LOG_INFO("Server", "Could not make a game controller for slot %u: %s",
                         static_cast<unsigned>(message.pad), error.c_str());
                return result;
            }
            slot.pad = std::move(pad);
            slot.haveSequence = false;
            slot.lastState = Wire::GamepadState();
            slot.name = LogSafe(message.name);
            LOG_INFO("Server", "Game controller %u connected ('%s', through %s)", static_cast<unsigned>(message.pad),
                     slot.name.c_str(), m_backend.GetName().c_str());
            return result;
        }

        bool GamepadHost::Update(const Wire::GamepadState& state)
        {
            if (state.pad >= m_maxPads)
            {
                return false;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            Slot& slot = m_slots[state.pad];
            if (slot.pad == nullptr)
            {
                return false;
            }
            if (slot.haveSequence && !Wire::IsNewerSequence(state.sequence, slot.lastSequence))
            {
                return false; // late or repeated
            }
            slot.haveSequence = true;
            slot.lastSequence = state.sequence;
            const bool changed = !state.SameInputs(slot.lastState);
            slot.lastState = state;
            if (!slot.pad->Update(state))
            {
                LOG_INFO("Server", "The computer refused a game controller's state (controller %u)",
                         static_cast<unsigned>(state.pad));
            }
            return changed;
        }

        void GamepadHost::Release(Slot& slot, uint8_t index)
        {
            if (slot.pad != nullptr)
            {
                // No stuck buttons: the game sees everything let go before the controller disappears.
                Wire::GamepadState neutral;
                neutral.pad = index;
                slot.pad->Update(neutral);
                slot.pad.reset();
                LOG_INFO("Server", "Game controller %u removed", static_cast<unsigned>(index));
            }
            slot.haveSequence = false;
            slot.lastState = Wire::GamepadState();
            slot.name.clear();
        }

        void GamepadHost::Disconnect(uint8_t pad)
        {
            if (pad >= Wire::kMaxGamepads)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            Release(m_slots[pad], pad);
        }

        void GamepadHost::DisconnectAll()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (uint8_t i = 0; i < Wire::kMaxGamepads; ++i)
            {
                Release(m_slots[i], i);
            }
        }

        int GamepadHost::GetCount() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            int count = 0;
            for (const Slot& slot : m_slots)
            {
                if (slot.pad != nullptr)
                {
                    ++count;
                }
            }
            return count;
        }
    } // namespace Server
} // namespace Marconi
