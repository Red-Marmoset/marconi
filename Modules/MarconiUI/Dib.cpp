// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows device-independent bitmaps (the clipboard image format other VNC programs exchange) to and from wxImage

#include "Dib.h"

#include <cstdint>
#include <cstring>

namespace MarconiUI
{
    namespace
    {
        constexpr uint32_t kInfoHeaderSize = 40;
        constexpr uint32_t kCompressionRgb = 0;
        constexpr uint32_t kCompressionBitfields = 3;
        constexpr int kMaxDimension = 32768;

        uint32_t ReadU32(const uint8_t* p)
        {
            return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                   static_cast<uint32_t>(p[3]) << 24;
        }

        uint16_t ReadU16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | p[1] << 8);
        }

        void PutU32(Marconi::Rfb::Bytes& out, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                out.push_back(static_cast<uint8_t>(value >> (8 * i)));
            }
        }

        void PutU16(Marconi::Rfb::Bytes& out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
        }

        // One colour channel from a masked pixel, scaled to 0-255.
        uint8_t Channel(uint32_t pixel, uint32_t mask)
        {
            if (mask == 0)
            {
                return 0;
            }
            int shift = 0;
            while (((mask >> shift) & 1) == 0)
            {
                ++shift;
            }
            uint32_t bits = mask >> shift;
            uint32_t value = (pixel & mask) >> shift;
            return static_cast<uint8_t>(bits == 0xFF ? value : value * 255 / bits);
        }
    } // namespace

    Marconi::Rfb::Bytes ImageToDib(const wxImage& image)
    {
        Marconi::Rfb::Bytes dib;
        if (!image.IsOk())
        {
            return dib;
        }
        int width = image.GetWidth();
        int height = image.GetHeight();
        size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
        dib.reserve(kInfoHeaderSize + stride * static_cast<size_t>(height));
        PutU32(dib, kInfoHeaderSize);
        PutU32(dib, static_cast<uint32_t>(width));
        PutU32(dib, static_cast<uint32_t>(height)); // positive: bottom-up
        PutU16(dib, 1);                               // planes
        PutU16(dib, 24);                              // bits per pixel
        PutU32(dib, kCompressionRgb);
        PutU32(dib, static_cast<uint32_t>(stride * static_cast<size_t>(height)));
        PutU32(dib, 2835); // pixels per metre, 72 dpi
        PutU32(dib, 2835);
        PutU32(dib, 0); // colours used
        PutU32(dib, 0); // colours important

        const unsigned char* rgb = image.GetData();
        for (int y = height - 1; y >= 0; --y)
        {
            const unsigned char* row = rgb + static_cast<size_t>(y) * static_cast<size_t>(width) * 3;
            size_t start = dib.size();
            dib.resize(start + stride, 0);
            uint8_t* out = dib.data() + start;
            for (int x = 0; x < width; ++x)
            {
                out[x * 3 + 0] = row[x * 3 + 2]; // DIB rows are B, G, R
                out[x * 3 + 1] = row[x * 3 + 1];
                out[x * 3 + 2] = row[x * 3 + 0];
            }
        }
        return dib;
    }

    wxImage DibToImage(const Marconi::Rfb::Bytes& dib)
    {
        if (dib.size() < kInfoHeaderSize)
        {
            return wxImage();
        }
        const uint8_t* data = dib.data();
        uint32_t headerSize = ReadU32(data);
        int32_t width = static_cast<int32_t>(ReadU32(data + 4));
        int32_t height = static_cast<int32_t>(ReadU32(data + 8));
        uint16_t bitCount = ReadU16(data + 14);
        uint32_t compression = ReadU32(data + 16);
        uint32_t coloursUsed = ReadU32(data + 32);
        bool topDown = height < 0;
        if (topDown)
        {
            height = -height;
        }
        if (headerSize < kInfoHeaderSize || headerSize > dib.size() || width <= 0 || height <= 0 ||
            width > kMaxDimension || height > kMaxDimension || (bitCount != 24 && bitCount != 32) ||
            (compression != kCompressionRgb && compression != kCompressionBitfields))
        {
            return wxImage();
        }

        uint32_t redMask = 0x00FF0000, greenMask = 0x0000FF00, blueMask = 0x000000FF;
        size_t offset = headerSize;
        if (compression == kCompressionBitfields)
        {
            // Masks live inside a V4/V5 header, or right after a plain 40-byte one.
            size_t maskOffset = headerSize > kInfoHeaderSize ? kInfoHeaderSize : headerSize;
            if (maskOffset + 12 > dib.size())
            {
                return wxImage();
            }
            redMask = ReadU32(data + maskOffset);
            greenMask = ReadU32(data + maskOffset + 4);
            blueMask = ReadU32(data + maskOffset + 8);
            if (headerSize == kInfoHeaderSize)
            {
                offset += 12;
            }
        }
        offset += static_cast<size_t>(coloursUsed) * 4; // an optional palette, which 24/32-bit pixels don't use
        size_t bytesPerPixel = bitCount / 8;
        size_t stride = (static_cast<size_t>(width) * bytesPerPixel + 3) & ~static_cast<size_t>(3);
        if (offset + stride * static_cast<size_t>(height) > dib.size())
        {
            return wxImage();
        }

        wxImage image(width, height, false);
        unsigned char* rgb = image.GetData();
        for (int y = 0; y < height; ++y)
        {
            int sourceRow = topDown ? y : height - 1 - y;
            const uint8_t* row = data + offset + static_cast<size_t>(sourceRow) * stride;
            unsigned char* out = rgb + static_cast<size_t>(y) * static_cast<size_t>(width) * 3;
            for (int x = 0; x < width; ++x)
            {
                const uint8_t* p = row + static_cast<size_t>(x) * bytesPerPixel;
                if (bitCount == 24)
                {
                    out[x * 3 + 0] = p[2];
                    out[x * 3 + 1] = p[1];
                    out[x * 3 + 2] = p[0];
                }
                else
                {
                    uint32_t pixel = ReadU32(p);
                    out[x * 3 + 0] = Channel(pixel, redMask);
                    out[x * 3 + 1] = Channel(pixel, greenMask);
                    out[x * 3 + 2] = Channel(pixel, blueMask);
                }
            }
        }
        return image;
    }
} // namespace MarconiUI
