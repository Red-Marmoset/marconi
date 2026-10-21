// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Working out the difference between the server's clock and this one from Ping/Pong times, so the server's
// timestamps can be compared with the client's

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Marconi
{
    namespace Wire
    {
        // Each sample is one Ping/Pong exchange: t0 when the client sent the Ping (client clock), t1 when the server
        // received it and t2 when it answered (server clock), t3 when the Pong arrived (client clock). If the
        // network took the same time each way, the server's clock is ahead of the client's by
        // ((t1 - t0) + (t2 - t3)) / 2, and the round trip took (t3 - t0) - (t2 - t1).
        //
        // A sample whose Pong waited behind a large picture on the server's write queue, or whose Ping waited
        // behind input on the client's, is lopsided and gives a wrong offset; such samples also have a longer
        // round trip. So the estimate uses the sample with the shortest round trip among the recent ones, the usual
        // filter for this (NTP does the same). Not thread-safe: the client uses it from its connection thread and
        // copies the results out.
        class ClockSync
        {
        public:
            explicit ClockSync(size_t window = 16) : m_window(window == 0 ? 1 : window) {}

            void AddSample(uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3);
            void Reset() { m_samples.clear(); }

            bool HasEstimate() const { return !m_samples.empty(); }
            // How far the server's clock is ahead of this one, in microseconds (negative if behind). 0 before the
            // first sample.
            int64_t GetOffsetMicros() const;
            // The round trip of the sample the offset came from, the best recent one; -1 before the first sample.
            int64_t GetBestRoundTripMicros() const;
            // The round trip of the newest sample, for showing the current state of the link; -1 before the first.
            int64_t GetLatestRoundTripMicros() const;

            // A server time expressed on this computer's clock.
            int64_t ToLocalMicros(uint64_t serverMicros) const
            {
                return static_cast<int64_t>(serverMicros) - GetOffsetMicros();
            }

        private:
            struct Sample
            {
                int64_t offset;
                int64_t roundTrip;
            };
            const Sample* Best() const;

            size_t m_window;
            std::deque<Sample> m_samples;
        };
    } // namespace Wire
} // namespace Marconi
