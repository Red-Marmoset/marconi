// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Client-side copy of the remote desktop: 32-bit 0x00RRGGBB pixels guarded by a mutex

#pragma once

#include <RmVncCore/Rfb/Messages.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace RmVnc
{
    namespace Rfb
    {
        // Written by the connection thread, read by the UI thread. Callers hold GetMutex() while touching pixels;
        // the drawing helpers below expect it to be held already.
        class Framebuffer
        {
        public:
            std::mutex& GetMutex() { return m_mutex; }

            void Resize(int width, int height);
            int GetWidth() const { return m_width; }
            int GetHeight() const { return m_height; }

            uint32_t* Row(int y) { return m_pixels.data() + static_cast<size_t>(y) * m_width; }
            const uint32_t* Row(int y) const { return m_pixels.data() + static_cast<size_t>(y) * m_width; }
            uint32_t GetPixel(int x, int y) const { return Row(y)[x]; }

            // True if `rect` lies entirely inside the framebuffer. Decoders reject anything else.
            bool Contains(const Rect& rect) const;

            void Fill(const Rect& rect, uint32_t colour);
            void CopyRect(const Rect& destination, int sourceX, int sourceY);
            void PutPixels(const Rect& rect, const uint32_t* pixels, size_t stride);

        private:
            std::mutex m_mutex;
            int m_width = 0;
            int m_height = 0;
            std::vector<uint32_t> m_pixels;
        };
    } // namespace Rfb
} // namespace RmVnc
