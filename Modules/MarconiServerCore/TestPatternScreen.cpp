// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The moving test pattern

#include "TestPatternScreen.h"

#include <algorithm>

namespace Marconi
{
    namespace Server
    {
        std::vector<Wire::Display> TestPatternScreen::GetDisplays()
        {
            Wire::Display display;
            display.id = 1;
            display.x = 0;
            display.y = 0;
            display.width = m_width;
            display.height = m_height;
            return {display};
        }

        void TestPatternScreen::Paint(Capture& out) const
        {
            out.width = m_width;
            out.height = m_height;
            out.pixels.resize(static_cast<size_t>(m_width) * m_height);
            int32_t bar = m_width > 0 ? static_cast<int32_t>(m_frame % static_cast<uint64_t>(m_width)) : 0;
            for (int32_t y = 0; y < m_height; ++y)
            {
                uint32_t* row = out.pixels.data() + static_cast<size_t>(y) * m_width;
                uint8_t green = m_height > 1 ? static_cast<uint8_t>(y * 255 / (m_height - 1)) : 0;
                for (int32_t x = 0; x < m_width; ++x)
                {
                    uint8_t red = m_width > 1 ? static_cast<uint8_t>(x * 255 / (m_width - 1)) : 0;
                    // The bar is white; everything else is a red/green gradient with blue carrying the frame count,
                    // so even a still photo of a single frame identifies it.
                    uint32_t colour = x == bar ? 0x00FFFFFFu
                                               : (static_cast<uint32_t>(red) << 16) |
                                                     (static_cast<uint32_t>(green) << 8) |
                                                     static_cast<uint8_t>(m_frame);
                    row[x] = colour;
                }
            }
        }

        bool TestPatternScreen::Grab(Capture& out)
        {
            Paint(out);
            int32_t bar = m_width > 0 ? static_cast<int32_t>(m_frame % static_cast<uint64_t>(m_width)) : 0;
            out.dirty.clear();
            if (m_lastBar < 0)
            {
                // First grab: the whole desktop is new.
                out.dirty.push_back({0, 0, m_width, m_height});
            }
            else
            {
                // Only the old bar's column (now redrawn as gradient) and the new bar's column changed. Report the
                // span between them, full height, as one rectangle.
                int32_t left = std::min(m_lastBar, bar);
                int32_t right = std::max(m_lastBar, bar);
                out.dirty.push_back({left, 0, right - left + 1, m_height});
            }
            m_lastBar = bar;
            ++m_frame;
            return true;
        }
    } // namespace Server
} // namespace Marconi
