// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clock comparison from Ping/Pong times

#include <MarconiCore/Wire/WireClockSync.h>

namespace Marconi
{
    namespace Wire
    {
        void ClockSync::AddSample(uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3)
        {
            const int64_t c0 = static_cast<int64_t>(t0);
            const int64_t s1 = static_cast<int64_t>(t1);
            const int64_t s2 = static_cast<int64_t>(t2);
            const int64_t c3 = static_cast<int64_t>(t3);
            if (c3 < c0 || s2 < s1)
            {
                return; // a clock went backwards, or the times are nonsense: ignore the sample
            }
            int64_t roundTrip = (c3 - c0) - (s2 - s1);
            if (roundTrip < 0)
            {
                roundTrip = 0; // the server took longer than the whole exchange: clocks of different resolution
            }
            Sample sample;
            sample.offset = ((s1 - c0) + (s2 - c3)) / 2;
            sample.roundTrip = roundTrip;
            m_samples.push_back(sample);
            while (m_samples.size() > m_window)
            {
                m_samples.pop_front();
            }
        }

        const ClockSync::Sample* ClockSync::Best() const
        {
            const Sample* best = nullptr;
            for (const Sample& sample : m_samples)
            {
                if (best == nullptr || sample.roundTrip < best->roundTrip)
                {
                    best = &sample;
                }
            }
            return best;
        }

        int64_t ClockSync::GetOffsetMicros() const
        {
            const Sample* best = Best();
            return best != nullptr ? best->offset : 0;
        }

        int64_t ClockSync::GetBestRoundTripMicros() const
        {
            const Sample* best = Best();
            return best != nullptr ? best->roundTrip : -1;
        }

        int64_t ClockSync::GetLatestRoundTripMicros() const
        {
            return m_samples.empty() ? -1 : m_samples.back().roundTrip;
        }
    } // namespace Wire
} // namespace Marconi
