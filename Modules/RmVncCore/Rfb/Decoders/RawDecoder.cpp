// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Raw encoding: uncompressed pixels, row by row

#include <RmVncCore/Rfb/Decoders/Decoders.h>

namespace RmVnc
{
    namespace Rfb
    {
        void RawDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            // Read in bands so a full-screen raw update doesn't hold the whole image twice, and the lock is
            // only held while copying, never while waiting on the network.
            const int bandHeight = std::max(1, 65536 / std::max(rect.width, 1));
            m_pixels.resize(static_cast<size_t>(rect.width) * std::min(bandHeight, rect.height));
            for (int y = 0; y < rect.height; y += bandHeight)
            {
                int rows = std::min(bandHeight, rect.height - y);
                size_t count = static_cast<size_t>(rect.width) * rows;
                for (size_t i = 0; i < count; ++i)
                {
                    m_pixels[i] = in.Pixel32() & 0x00FFFFFFu;
                }
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                framebuffer.PutPixels({rect.x, rect.y + y, rect.width, rows}, m_pixels.data(), rect.width);
            }
        }
    } // namespace Rfb
} // namespace RmVnc
