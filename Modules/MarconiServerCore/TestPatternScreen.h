// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A ScreenSource with no real display: a moving pattern with a frame counter, for tests and for trying the server
// on a machine with nothing to capture (the --test-pattern mode)

#pragma once

#include "ScreenSource.h"

namespace Marconi
{
    namespace Server
    {
        // Draws a colour gradient with a bright vertical bar that steps one column to the right each Grab, so a
        // viewer sees steady motion and a photo of both screens shows how far behind the client is. Reports the
        // columns that changed as the dirty area, so partial-update handling gets exercised too.
        class TestPatternScreen : public ScreenSource
        {
        public:
            TestPatternScreen(int32_t width, int32_t height) : m_width(width), m_height(height) {}

            std::vector<Wire::Display> GetDisplays() override;
            bool Grab(Capture& out) override;

            uint64_t FrameCount() const { return m_frame; }

        private:
            void Paint(Capture& out) const;

            int32_t m_width;
            int32_t m_height;
            uint64_t m_frame = 0;
            int32_t m_lastBar = -1; // the bar column in the previous grab, -1 before the first
        };
    } // namespace Server
} // namespace Marconi
