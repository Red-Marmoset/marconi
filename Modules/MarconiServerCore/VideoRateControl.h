// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Chooses the video bitrate from what the viewer asks for and how its reports look

#pragma once

#include <MarconiCore/Wire/WireVideoMessages.h>

#include <cstdint>

namespace Marconi
{
    namespace Server
    {
        // The rule, written down so it can be argued with:
        //  - The ceiling is the smallest of the wishes that are set: the server's own limit, the viewer's StreamConfig
        //    maximum, the viewer's latest target. A quality wish (1..100) scales the starting point and the ceiling
        //    down for low values (a viewer that prefers speed gets fewer bits per picture).
        //  - A report with drops, a deep queue or a decode time over 90% of a frame's time is congestion: the rate
        //    falls to three quarters (never below the floor). Four clean reports in a row raise it by an eighth, up
        //    to the ceiling. So the rate settles just under what the viewer and the link can take.
        class VideoRateControl
        {
        public:
            VideoRateControl(uint32_t serverCeilingKbps, uint32_t startKbps, uint32_t fps);

            void SetFps(uint32_t fps) { m_fps = fps > 0 ? fps : 1; }
            void SetConfigMaximum(uint32_t kbps); // from StreamConfig; 0 = none
            // Takes a report; returns true if the bitrate changed (read it with Current()).
            bool OnFeedback(const Wire::VideoFeedback& feedback);

            uint32_t Current() const { return m_current; }
            uint32_t Ceiling() const;

            static constexpr uint32_t kFloorKbps = 300;

        private:
            bool Clamp();

            uint32_t m_serverCeiling;
            uint32_t m_configMax = 0;
            uint32_t m_viewerTarget = 0;
            uint32_t m_quality = 0;
            uint32_t m_fps;
            uint32_t m_current;
            int m_cleanReports = 0;
        };
    } // namespace Server
} // namespace Marconi
