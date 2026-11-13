// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Notices that a viewer has sent nothing for too long

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

namespace Marconi
{
    namespace Server
    {
        // Where "now" comes from, so tests can move time by hand. The default is the steady clock.
        using IdleClock = std::function<std::chrono::steady_clock::time_point()>;

        // Counts the time since the viewer last did something. `Touch` is called whenever input arrives (from one
        // thread); `Expired` is asked from another. A limit of 0 minutes means never.
        class IdleWatch
        {
        public:
            explicit IdleWatch(IdleClock clock = nullptr) : m_clock(std::move(clock)) { Touch(); }

            // Replaces the clock and starts counting again from its "now". Call before the session runs.
            void SetClock(IdleClock clock)
            {
                m_clock = std::move(clock);
                Touch();
            }

            void SetLimitMinutes(int minutes) { m_limitMinutes = minutes < 0 ? 0 : minutes; }
            int GetLimitMinutes() const { return m_limitMinutes; }

            void Touch() { m_lastTicks = Now().time_since_epoch().count(); }

            bool Expired() const
            {
                int minutes = m_limitMinutes;
                if (minutes <= 0)
                {
                    return false;
                }
                std::chrono::steady_clock::time_point last{std::chrono::steady_clock::duration(m_lastTicks.load())};
                return Now() - last >= std::chrono::minutes(minutes);
            }

        private:
            std::chrono::steady_clock::time_point Now() const
            {
                return m_clock ? m_clock() : std::chrono::steady_clock::now();
            }

            IdleClock m_clock;
            std::atomic<int> m_limitMinutes{0};
            std::atomic<int64_t> m_lastTicks{0};
        };
    } // namespace Server
} // namespace Marconi
