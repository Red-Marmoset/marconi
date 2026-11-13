// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Works out which 64x64 tiles of a screen picture changed since the last one (shared by the macOS and Linux capture)

#pragma once

#include "ScreenSource.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // Compares each new picture with the one before, tile by tile; tiles that changed side by side in a row
        // become one rectangle. The first picture, and the first after the size changes, is all dirty.
        class DirtyTracker
        {
        public:
            static constexpr int kTile = 64;

            // Fills `capture.dirty` from `capture.pixels` (width*height) and remembers the picture for next time.
            void Update(Capture& capture)
            {
                capture.dirty.clear();
                const int width = capture.width;
                const int height = capture.height;
                const bool first = m_previous.size() != capture.pixels.size() || m_width != width || m_height != height;
                if (first)
                {
                    capture.dirty.push_back({0, 0, width, height});
                }
                else
                {
                    for (int ty = 0; ty < height; ty += kTile)
                    {
                        const int tileHeight = std::min(kTile, height - ty);
                        int runStart = -1;
                        for (int tx = 0; tx <= width; tx += kTile)
                        {
                            bool changed = false;
                            if (tx < width)
                            {
                                const int tileWidth = std::min(kTile, width - tx);
                                for (int y = ty; y < ty + tileHeight && !changed; ++y)
                                {
                                    const size_t offset = static_cast<size_t>(y) * width + tx;
                                    changed = std::memcmp(capture.pixels.data() + offset, m_previous.data() + offset,
                                                          static_cast<size_t>(tileWidth) * 4) != 0;
                                }
                            }
                            if (changed && runStart < 0)
                            {
                                runStart = tx;
                            }
                            else if (!changed && runStart >= 0)
                            {
                                capture.dirty.push_back({runStart, ty, std::min(tx, width) - runStart, tileHeight});
                                runStart = -1;
                            }
                        }
                    }
                }
                m_previous = capture.pixels;
                m_width = width;
                m_height = height;
            }

            // Forgets the last picture, so the next one is all dirty.
            void Reset()
            {
                m_previous.clear();
                m_width = 0;
                m_height = 0;
            }

        private:
            std::vector<uint32_t> m_previous;
            int m_width = 0;
            int m_height = 0;
        };
    } // namespace Server
} // namespace Marconi
