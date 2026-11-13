// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Round-trip estimation, the bitrate hint and the status text of the datagram channel

#include <MarconiCore/Wire/DatagramStats.h>

#include <algorithm>
#include <cstdlib>

namespace Marconi
{
    namespace Wire
    {
        void RttEstimator::Sample(Micros rttUs)
        {
            rttUs = std::max<Micros>(rttUs, 1);
            if (m_count == 0)
            {
                m_smoothed = rttUs;
                m_variation = rttUs / 2;
            }
            else
            {
                Micros difference = m_smoothed > rttUs ? m_smoothed - rttUs : rttUs - m_smoothed;
                m_variation = (3 * m_variation + difference) / 4;
                m_smoothed = (7 * m_smoothed + rttUs) / 8;
            }
            m_recent[m_count % 32] = rttUs;
            ++m_count;
        }

        Micros RttEstimator::Minimum() const
        {
            uint32_t available = std::min<uint32_t>(m_count, 32);
            if (available == 0)
            {
                return 0;
            }
            return *std::min_element(m_recent, m_recent + available);
        }

        BitrateHint ComputeBitrateHint(const DatagramStats& stats)
        {
            BitrateHint hint;
            double loss = stats.havePeerReport ? stats.peerLossPercent : stats.lossPercent;
            bool queueing = false;
            bool clear = true;
            if (stats.haveRtt && stats.minRttUs > 0)
            {
                queueing = stats.rttUs > stats.minRttUs * 2 + 30000;
                clear = stats.rttUs <= stats.minRttUs + stats.minRttUs / 4 + 10000;
            }
            if (stats.state != LinkState::Active)
            {
                return hint; // nothing to say about a link we are not using
            }
            if (loss >= 10.0)
            {
                hint.advice = BitrateAdvice::Decrease;
                hint.scalePercent = 70;
            }
            else if (loss >= 3.0 || queueing)
            {
                hint.advice = BitrateAdvice::Decrease;
                hint.scalePercent = 85;
            }
            else if (loss < 0.5 && clear)
            {
                hint.advice = BitrateAdvice::Increase;
                hint.scalePercent = 110;
            }
            return hint;
        }

        std::string DatagramStatusText(const DatagramStats& stats)
        {
            switch (stats.state)
            {
            case LinkState::Idle:
                return std::string();
            case LinkState::Probing:
                return "UDP starting";
            case LinkState::FellBack:
                return "over TCP";
            case LinkState::Active:
            {
                int loss = static_cast<int>(stats.lossPercent + 0.5);
                if (loss >= 1)
                {
                    return "UDP, " + std::to_string(loss) + "% loss";
                }
                return "UDP";
            }
            }
            return std::string();
        }
    } // namespace Wire
} // namespace Marconi
