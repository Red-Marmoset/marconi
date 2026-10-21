// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A ScreenSource with no real display: a moving pattern with a frame counter, for tests and for trying the server
// on a machine with nothing to capture (the --test-pattern mode)

#pragma once

#include "InputSink.h"
#include "ScreenSource.h"

#include <atomic>

namespace Marconi
{
    namespace Server
    {
        // Draws a colour gradient with a bright vertical bar that steps one column to the right each Grab, so a
        // viewer sees steady motion. Reports the columns that changed as the dirty area, so partial-update handling
        // gets exercised too.
        //
        // When the pattern is large enough (kPanelWidth by kPanelHeight and up) it also draws, in large digits in
        // the top-left corner, the frame number and the server's clock in milliseconds (Wire::NowMicros / 1000),
        // and a cross where the viewer's pointer last was (red while a button is held). Filming the client's screen
        // next to its performance readout, which shows the server's clock as the client estimates it, then gives
        // how long a picture takes to reach the screen, and moving the mouse shows how long input takes to come
        // back as a picture (PERFORMANCE_PLAN.md step 1).
        class TestPatternScreen : public ScreenSource
        {
        public:
            static constexpr int32_t kPanelWidth = 232;
            static constexpr int32_t kPanelHeight = 108;

            TestPatternScreen(int32_t width, int32_t height) : m_width(width), m_height(height) {}

            std::vector<Wire::Display> GetDisplays() override;
            bool Grab(Capture& out) override;

            uint64_t FrameCount() const { return m_frame; }

            // Where the viewer's pointer is, for the cross. Any thread (the session's input thread calls it through
            // PatternEchoInput).
            void SetPointer(int32_t x, int32_t y, uint8_t buttons);

            // The panel's area, or an empty rectangle when the pattern is too small to carry it.
            Wire::Rect PanelRect() const;

        private:
            void Paint(Capture& out, uint64_t clockMillis, int32_t pointerX, int32_t pointerY, bool pressed) const;
            Wire::Rect PointerRect(int32_t x, int32_t y) const;

            int32_t m_width;
            int32_t m_height;
            uint64_t m_frame = 0;
            int32_t m_lastBar = -1; // the bar column in the previous grab, -1 before the first
            // The pointer, packed as x (bits 0-23), y (24-47), buttons (48-55); all ones until the first report.
            std::atomic<uint64_t> m_pointer{~uint64_t{0}};
            int32_t m_drawnPointerX = -1; // where the cross was drawn in the previous grab; -1 for none
            int32_t m_drawnPointerY = -1;
            bool m_drawnPressed = false;
        };

        // Passes input on to another sink, and tells the test pattern where the pointer is so it can draw it.
        class PatternEchoInput : public InputSink
        {
        public:
            PatternEchoInput(InputSink& inner, TestPatternScreen& pattern) : m_inner(inner), m_pattern(pattern) {}

            void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
            {
                m_pattern.SetPointer(x, y, buttons);
                m_inner.MovePointer(x, y, buttons, wheel, wheelX);
            }
            void SendKey(uint32_t keysym, uint32_t scancode, bool down) override
            {
                m_inner.SendKey(keysym, scancode, down);
            }
            void TypeText(const std::string& utf8) override { m_inner.TypeText(utf8); }

        private:
            InputSink& m_inner;
            TestPatternScreen& m_pattern;
        };
    } // namespace Server
} // namespace Marconi
