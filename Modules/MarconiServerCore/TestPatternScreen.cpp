// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The moving test pattern

#include "TestPatternScreen.h"

#include <algorithm>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // The panel sits this far from the top-left corner.
            constexpr int32_t kPanelMargin = 8;
            // Seven-segment digits: cell size, stroke, the gap between digits, and the padding inside the panel.
            constexpr int32_t kDigitWidth = 20;
            constexpr int32_t kDigitHeight = 36;
            constexpr int32_t kStroke = 4;
            constexpr int32_t kDigitGap = 6;
            constexpr int32_t kPadding = 12;
            constexpr int kDigits = 8;
            // Half the size of the pointer cross.
            constexpr int32_t kCrossRadius = 5;

            constexpr uint32_t kWhite = 0x00FFFFFFu;
            constexpr uint32_t kYellow = 0x00FFFF00u;
            constexpr uint32_t kRed = 0x00FF0000u;
            constexpr uint32_t kBlack = 0x00000000u;

            // Which segments each digit lights: bit 0 top, 1 top right, 2 bottom right, 3 bottom, 4 bottom left,
            // 5 top left, 6 middle.
            constexpr uint8_t kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

            void FillRect(Capture& out, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t colour)
            {
                const int32_t left = std::max(0, x);
                const int32_t top = std::max(0, y);
                const int32_t right = std::min(out.width, x + width);
                const int32_t bottom = std::min(out.height, y + height);
                for (int32_t row = top; row < bottom; ++row)
                {
                    uint32_t* line = out.pixels.data() + static_cast<size_t>(row) * out.width;
                    std::fill(line + left, line + std::max(left, right), colour);
                }
            }

            void DrawDigit(Capture& out, int32_t x, int32_t y, int digit, uint32_t colour)
            {
                const uint8_t lit = kSegments[digit % 10];
                const int32_t half = kDigitHeight / 2;
                if (lit & 0x01)
                {
                    FillRect(out, x, y, kDigitWidth, kStroke, colour);
                }
                if (lit & 0x02)
                {
                    FillRect(out, x + kDigitWidth - kStroke, y, kStroke, half, colour);
                }
                if (lit & 0x04)
                {
                    FillRect(out, x + kDigitWidth - kStroke, y + half, kStroke, half, colour);
                }
                if (lit & 0x08)
                {
                    FillRect(out, x, y + kDigitHeight - kStroke, kDigitWidth, kStroke, colour);
                }
                if (lit & 0x10)
                {
                    FillRect(out, x, y + half, kStroke, half, colour);
                }
                if (lit & 0x20)
                {
                    FillRect(out, x, y, kStroke, half, colour);
                }
                if (lit & 0x40)
                {
                    FillRect(out, x, y + half - kStroke / 2, kDigitWidth, kStroke, colour);
                }
            }

            // The last kDigits decimal digits of `value`, with leading zeros, left to right.
            void DrawNumber(Capture& out, int32_t x, int32_t y, uint64_t value, uint32_t colour)
            {
                for (int i = kDigits - 1; i >= 0; --i)
                {
                    DrawDigit(out, x + i * (kDigitWidth + kDigitGap), y, static_cast<int>(value % 10), colour);
                    value /= 10;
                }
            }

            Wire::Rect Clip(const Wire::Rect& rect, int32_t width, int32_t height)
            {
                const int32_t left = std::max(0, rect.x);
                const int32_t top = std::max(0, rect.y);
                const int32_t right = std::min(width, rect.x + rect.width);
                const int32_t bottom = std::min(height, rect.y + rect.height);
                if (right <= left || bottom <= top)
                {
                    return {0, 0, 0, 0};
                }
                return {left, top, right - left, bottom - top};
            }
        } // namespace

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

        void TestPatternScreen::SetPointer(int32_t x, int32_t y, uint8_t buttons)
        {
            const uint64_t packed = (static_cast<uint64_t>(x) & 0xFFFFFF) | ((static_cast<uint64_t>(y) & 0xFFFFFF) << 24) |
                                    (static_cast<uint64_t>(buttons) << 48);
            m_pointer = packed;
        }

        Wire::Rect TestPatternScreen::PanelRect() const
        {
            if (m_width < kPanelWidth + 2 * kPanelMargin || m_height < kPanelHeight + 2 * kPanelMargin)
            {
                return {0, 0, 0, 0};
            }
            return {kPanelMargin, kPanelMargin, kPanelWidth, kPanelHeight};
        }

        Wire::Rect TestPatternScreen::PointerRect(int32_t x, int32_t y) const
        {
            return Clip({x - kCrossRadius, y - kCrossRadius, 2 * kCrossRadius + 1, 2 * kCrossRadius + 1}, m_width,
                        m_height);
        }

        void TestPatternScreen::Paint(Capture& out, uint64_t clockMillis, int32_t pointerX, int32_t pointerY,
                                      bool pressed) const
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

            const Wire::Rect panel = PanelRect();
            if (panel.width > 0)
            {
                FillRect(out, panel.x, panel.y, panel.width, panel.height, kBlack);
                const int32_t x = panel.x + kPadding;
                DrawNumber(out, x, panel.y + kPadding, m_frame, kWhite);
                DrawNumber(out, x, panel.y + kPadding + kDigitHeight + kPadding, clockMillis, kYellow);
            }

            if (pointerX >= 0 && pointerY >= 0)
            {
                const uint32_t colour = pressed ? kRed : kWhite;
                FillRect(out, pointerX - kCrossRadius, pointerY - 1, 2 * kCrossRadius + 1, 3, colour);
                FillRect(out, pointerX - 1, pointerY - kCrossRadius, 3, 2 * kCrossRadius + 1, colour);
            }
        }

        bool TestPatternScreen::Grab(Capture& out)
        {
            const uint64_t clockMillis = Wire::NowMicros() / 1000;
            int32_t pointerX = -1;
            int32_t pointerY = -1;
            bool pressed = false;
            const uint64_t pointer = m_pointer.load();
            if (pointer != ~uint64_t{0})
            {
                pointerX = static_cast<int32_t>(pointer & 0xFFFFFF);
                pointerY = static_cast<int32_t>((pointer >> 24) & 0xFFFFFF);
                pressed = ((pointer >> 48) & 0xFF) != 0;
                if (pointerX >= m_width || pointerY >= m_height)
                {
                    pointerX = -1; // off the pattern (a larger desktop on the client): nothing to draw
                    pointerY = -1;
                }
            }
            Paint(out, clockMillis, pointerX, pointerY, pressed);

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

                // The digits change every frame; the cross only when the pointer moves or a button changes.
                const Wire::Rect panel = PanelRect();
                if (panel.width > 0)
                {
                    out.dirty.push_back(panel);
                }
                if (pointerX != m_drawnPointerX || pointerY != m_drawnPointerY || pressed != m_drawnPressed)
                {
                    if (m_drawnPointerX >= 0)
                    {
                        Wire::Rect old = PointerRect(m_drawnPointerX, m_drawnPointerY);
                        if (old.width > 0)
                        {
                            out.dirty.push_back(old);
                        }
                    }
                    if (pointerX >= 0)
                    {
                        Wire::Rect now = PointerRect(pointerX, pointerY);
                        if (now.width > 0)
                        {
                            out.dirty.push_back(now);
                        }
                    }
                }
            }
            m_drawnPointerX = pointerX;
            m_drawnPointerY = pointerY;
            m_drawnPressed = pressed;
            m_lastBar = bar;
            ++m_frame;
            return true;
        }
    } // namespace Server
} // namespace Marconi
