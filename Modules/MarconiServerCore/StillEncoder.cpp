// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Lossless rectangles for the Still message

#include "StillEncoder.h"

#include <MarconiCore/Wire/WireCompress.h>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // Packs the pixels of one rectangle of a capture into a Still's little-endian 0x00RRGGBB bytes
            // (byte order B, G, R, 0), row by row.
            Wire::Bytes PackRect(const Capture& capture, const Wire::Rect& rect)
            {
                Wire::Bytes data;
                data.reserve(static_cast<size_t>(rect.width) * rect.height * 4);
                for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
                {
                    const uint32_t* row = capture.Row(y);
                    for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
                    {
                        uint32_t pixel = row[x];
                        data.push_back(static_cast<uint8_t>(pixel));
                        data.push_back(static_cast<uint8_t>(pixel >> 8));
                        data.push_back(static_cast<uint8_t>(pixel >> 16));
                        data.push_back(0);
                    }
                }
                return data;
            }
        } // namespace

        bool RectInside(const Wire::Rect& rect, int32_t width, int32_t height)
        {
            return rect.width > 0 && rect.height > 0 && rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= width &&
                   rect.y + rect.height <= height;
        }

        void EncodeStill(const Capture& capture, const Wire::Rect& rect, Wire::Still& still, StillTiming* timing)
        {
            const uint64_t started = Wire::NowMicros();
            still.rect = rect;
            Wire::Bytes raw = PackRect(capture, rect);
            const uint64_t packed = Wire::NowMicros();
            Wire::Bytes compressed = Wire::ZlibCompress(raw);
            const uint64_t done = Wire::NowMicros();
            if (!compressed.empty() && compressed.size() < raw.size())
            {
                still.compression = Wire::Still::Compression::Zlib;
                still.data = std::move(compressed);
            }
            else
            {
                still.compression = Wire::Still::Compression::None;
                still.data = std::move(raw);
            }
            if (timing != nullptr)
            {
                timing->packMicros = packed - started;
                timing->compressMicros = done - packed;
            }
        }
    } // namespace Server
} // namespace Marconi
