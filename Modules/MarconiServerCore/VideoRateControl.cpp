// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Chooses the video bitrate from what the viewer asks for and how its reports look

#include "VideoRateControl.h"

#include <algorithm>

namespace Marconi
{
    namespace Server
    {
        VideoRateControl::VideoRateControl(uint32_t serverCeilingKbps, uint32_t startKbps, uint32_t fps)
            : m_serverCeiling(serverCeilingKbps), m_fps(fps > 0 ? fps : 1), m_current(startKbps)
        {
            Clamp();
        }

        uint32_t VideoRateControl::Ceiling() const
        {
            uint32_t ceiling = m_serverCeiling;
            if (m_configMax > 0)
            {
                ceiling = std::min(ceiling, m_configMax);
            }
            if (m_viewerTarget > 0)
            {
                ceiling = std::min(ceiling, m_viewerTarget);
            }
            if (m_quality > 0 && m_quality < 100)
            {
                // Quality 100 keeps the whole ceiling; 1 allows a quarter of it.
                ceiling = static_cast<uint32_t>(static_cast<uint64_t>(ceiling) * (25 + 75 * m_quality / 100) / 100);
            }
            return std::max(ceiling, kFloorKbps);
        }

        bool VideoRateControl::Clamp()
        {
            uint32_t before = m_current;
            m_current = std::min(std::max(m_current, kFloorKbps), Ceiling());
            return m_current != before;
        }

        void VideoRateControl::SetConfigMaximum(uint32_t kbps)
        {
            m_configMax = kbps;
            Clamp();
        }

        bool VideoRateControl::OnFeedback(const Wire::VideoFeedback& feedback)
        {
            const uint32_t before = m_current;
            m_viewerTarget = feedback.targetBitrateKbps;
            m_quality = feedback.quality;
            const uint32_t frameMicros = 1000000u / m_fps;
            const bool congested = feedback.framesDropped > 0 || feedback.queueDepth >= 3 ||
                                   (feedback.framesReceived > 0 && feedback.decodeMicros > frameMicros * 9 / 10);
            if (congested)
            {
                m_cleanReports = 0;
                m_current = std::max(kFloorKbps, m_current * 3 / 4);
            }
            else if (++m_cleanReports >= 4)
            {
                m_cleanReports = 0;
                m_current += std::max<uint32_t>(m_current / 8, 1);
            }
            Clamp(); // a lowered wish takes effect straight away
            return m_current != before;
        }
    } // namespace Server
} // namespace Marconi
