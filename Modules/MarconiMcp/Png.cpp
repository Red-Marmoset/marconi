// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Writing screenshots as PNG files, with wx's bundled zlib

#include <MarconiMcp/Png.h>

#include <zlib.h> // wx's bundled zlib (symbols prefixed wx_zlib_)

#include <cstring>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            void PutBigEndian32(std::vector<uint8_t>& out, uint32_t value)
            {
                out.push_back(static_cast<uint8_t>(value >> 24));
                out.push_back(static_cast<uint8_t>(value >> 16));
                out.push_back(static_cast<uint8_t>(value >> 8));
                out.push_back(static_cast<uint8_t>(value));
            }

            // Length, type, data, then the CRC of type and data.
            void PutChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t length)
            {
                PutBigEndian32(out, static_cast<uint32_t>(length));
                const size_t typeAt = out.size();
                out.insert(out.end(), type, type + 4);
                if (length > 0)
                {
                    out.insert(out.end(), data, data + length);
                }
                uLong crc = crc32(0L, Z_NULL, 0);
                crc = crc32(crc, out.data() + typeAt, static_cast<uInt>(4 + length));
                PutBigEndian32(out, static_cast<uint32_t>(crc));
            }
        } // namespace

        std::vector<uint8_t> EncodePng(const uint32_t* pixels, int width, int height, size_t stride)
        {
            if (width <= 0 || height <= 0 || pixels == nullptr)
            {
                return {};
            }

            // Each row: filter type 1 ("Sub", each byte minus the one three to its left), then the RGB bytes.
            // Screens have large flat areas, which Sub turns into runs of zeros that compress well.
            const size_t rowBytes = 1 + static_cast<size_t>(width) * 3;
            std::vector<uint8_t> raw(rowBytes * static_cast<size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                const uint32_t* row = pixels + static_cast<size_t>(y) * stride;
                uint8_t* out = raw.data() + static_cast<size_t>(y) * rowBytes;
                *out++ = 1;
                uint8_t previousR = 0;
                uint8_t previousG = 0;
                uint8_t previousB = 0;
                for (int x = 0; x < width; ++x)
                {
                    const uint8_t r = static_cast<uint8_t>(row[x] >> 16);
                    const uint8_t g = static_cast<uint8_t>(row[x] >> 8);
                    const uint8_t b = static_cast<uint8_t>(row[x]);
                    *out++ = static_cast<uint8_t>(r - previousR);
                    *out++ = static_cast<uint8_t>(g - previousG);
                    *out++ = static_cast<uint8_t>(b - previousB);
                    previousR = r;
                    previousG = g;
                    previousB = b;
                }
            }

            uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
            std::vector<uint8_t> compressed(compressedSize);
            if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), 3) != Z_OK)
            {
                return {};
            }

            std::vector<uint8_t> png;
            png.reserve(compressedSize + 64);
            static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
            png.insert(png.end(), kSignature, kSignature + 8);

            std::vector<uint8_t> header;
            PutBigEndian32(header, static_cast<uint32_t>(width));
            PutBigEndian32(header, static_cast<uint32_t>(height));
            header.push_back(8); // bits per channel
            header.push_back(2); // colour type: RGB
            header.push_back(0); // compression: deflate
            header.push_back(0); // filtering: adaptive (per-row filter byte)
            header.push_back(0); // no interlacing
            PutChunk(png, "IHDR", header.data(), header.size());
            PutChunk(png, "IDAT", compressed.data(), compressedSize);
            PutChunk(png, "IEND", nullptr, 0);
            return png;
        }
    } // namespace Mcp
} // namespace Marconi
