// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Client-side copy of the remote desktop: 32-bit 0x00RRGGBB pixels guarded by a mutex

#include <MarconiCore/Rfb/Framebuffer.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rfb
    {
        void Framebuffer::Resize(int width, int height)
        {
            width = std::max(width, 0);
            height = std::max(height, 0);
            // Starts black: the old pixels belong to a different layout, and a Mac switching between its scaled
            // and native modes would otherwise leave a misleading corner of the old picture on screen.
            std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0);
            m_pixels.swap(pixels);
            m_width = width;
            m_height = height;
        }

        bool Framebuffer::IsBlack() const
        {
            for (uint32_t pixel : m_pixels)
            {
                if ((pixel & 0x00FFFFFFu) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool Framebuffer::Contains(const Rect& rect) const
        {
            return rect.x >= 0 && rect.y >= 0 && rect.width >= 0 && rect.height >= 0 && rect.Right() <= m_width &&
                   rect.Bottom() <= m_height;
        }

        void Framebuffer::Fill(const Rect& rect, uint32_t colour)
        {
            for (int y = rect.y; y < rect.Bottom(); ++y)
            {
                std::fill_n(Row(y) + rect.x, rect.width, colour);
            }
        }

        void Framebuffer::CopyRect(const Rect& destination, int sourceX, int sourceY)
        {
            // Rows are copied in the order that never overwrites unread source pixels; memmove handles
            // horizontal overlap within a row.
            size_t rowBytes = static_cast<size_t>(destination.width) * sizeof(uint32_t);
            if (sourceY < destination.y)
            {
                for (int row = destination.height - 1; row >= 0; --row)
                {
                    std::memmove(Row(destination.y + row) + destination.x, Row(sourceY + row) + sourceX, rowBytes);
                }
            }
            else
            {
                for (int row = 0; row < destination.height; ++row)
                {
                    std::memmove(Row(destination.y + row) + destination.x, Row(sourceY + row) + sourceX, rowBytes);
                }
            }
        }

        void Framebuffer::PutPixels(const Rect& rect, const uint32_t* pixels, size_t stride)
        {
            for (int row = 0; row < rect.height; ++row)
            {
                std::memcpy(Row(rect.y + row) + rect.x, pixels + row * stride, rect.width * sizeof(uint32_t));
            }
        }
    } // namespace Rfb
} // namespace Marconi
