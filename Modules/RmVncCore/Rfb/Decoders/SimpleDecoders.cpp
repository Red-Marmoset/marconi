// Copyright (c) 2026 Red Marmoset Studios Ltd.
// CopyRect, RRE and Hextile decoders

#include <RmVncCore/Rfb/Decoders/Decoders.h>

namespace RmVnc
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kColourMask = 0x00FFFFFFu;
            constexpr uint32_t kMaxRreSubrects = 1u << 24;

            bool Inside(int x, int y, int w, int h, int width, int height)
            {
                return x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= width && y + h <= height;
            }
        } // namespace

        void CopyRectDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            int sourceX = in.U16();
            int sourceY = in.U16();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            if (!framebuffer.Contains({sourceX, sourceY, rect.width, rect.height}))
            {
                throw RfbError("CopyRect source lies outside the desktop");
            }
            framebuffer.CopyRect(rect, sourceX, sourceY);
        }

        void RreDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            uint32_t count = in.U32();
            if (count > kMaxRreSubrects)
            {
                throw RfbError("RRE rectangle has too many subrectangles");
            }
            m_buffer.Resize(rect.width, rect.height);
            m_buffer.Fill(0, 0, rect.width, rect.height, in.Pixel32() & kColourMask);
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t colour = in.Pixel32() & kColourMask;
                int x = in.U16();
                int y = in.U16();
                int w = in.U16();
                int h = in.U16();
                if (!Inside(x, y, w, h, rect.width, rect.height))
                {
                    throw RfbError("RRE subrectangle lies outside its rectangle");
                }
                m_buffer.Fill(x, y, w, h, colour);
            }
            m_buffer.CopyTo(framebuffer, rect.x, rect.y);
        }

        void HextileDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            enum : uint8_t
            {
                Raw = 1,
                BackgroundSpecified = 2,
                ForegroundSpecified = 4,
                AnySubrects = 8,
                SubrectsColoured = 16,
            };

            m_buffer.Resize(rect.width, rect.height);
            // Background and foreground carry over from tile to tile within a rectangle.
            uint32_t background = 0;
            uint32_t foreground = 0;

            for (int tileY = 0; tileY < rect.height; tileY += 16)
            {
                int tileHeight = std::min(16, rect.height - tileY);
                for (int tileX = 0; tileX < rect.width; tileX += 16)
                {
                    int tileWidth = std::min(16, rect.width - tileX);
                    uint8_t flags = in.U8();

                    if (flags & Raw)
                    {
                        for (int y = 0; y < tileHeight; ++y)
                        {
                            uint32_t* row = m_buffer.Row(tileY + y) + tileX;
                            for (int x = 0; x < tileWidth; ++x)
                            {
                                row[x] = in.Pixel32() & kColourMask;
                            }
                        }
                        continue;
                    }

                    if (flags & BackgroundSpecified)
                    {
                        background = in.Pixel32() & kColourMask;
                    }
                    if (flags & ForegroundSpecified)
                    {
                        foreground = in.Pixel32() & kColourMask;
                    }
                    m_buffer.Fill(tileX, tileY, tileWidth, tileHeight, background);

                    if (flags & AnySubrects)
                    {
                        uint8_t count = in.U8();
                        for (uint8_t i = 0; i < count; ++i)
                        {
                            uint32_t colour = (flags & SubrectsColoured) ? (in.Pixel32() & kColourMask) : foreground;
                            uint8_t position = in.U8();
                            uint8_t size = in.U8();
                            int x = position >> 4;
                            int y = position & 15;
                            int w = (size >> 4) + 1;
                            int h = (size & 15) + 1;
                            if (!Inside(x, y, w, h, tileWidth, tileHeight))
                            {
                                throw RfbError("Hextile subrectangle lies outside its tile");
                            }
                            m_buffer.Fill(tileX + x, tileY + y, w, h, colour);
                        }
                    }
                }
            }
            m_buffer.CopyTo(framebuffer, rect.x, rect.y);
        }
    } // namespace Rfb
} // namespace RmVnc
