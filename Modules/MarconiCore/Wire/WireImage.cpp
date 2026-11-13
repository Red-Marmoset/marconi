// Copyright (c) 2026 Red Marmoset Studios Ltd.
// PNG and DIB conversion for clipboard images

#include "WireImage.h"

#include <MarconiCore/Wire/WireCompress.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(disable : 4505)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4456 4457 4701)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <cstring>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            uint32_t ReadU32(const uint8_t* p)
            {
                return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
                       static_cast<uint32_t>(p[3]) << 24;
            }

            uint16_t ReadU16(const uint8_t* p)
            {
                return static_cast<uint16_t>(p[0] | p[1] << 8);
            }

            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
                }
            }

            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value));
                out.push_back(static_cast<uint8_t>(value >> 8));
            }

            void PutBigU32(Bytes& out, uint32_t value)
            {
                for (int i = 3; i >= 0; --i)
                {
                    out.push_back(static_cast<uint8_t>(value >> (8 * i)));
                }
            }

            uint32_t Crc32(const uint8_t* data, size_t length)
            {
                static uint32_t table[256];
                static bool ready = false;
                if (!ready) // benign race: every thread computes the same table
                {
                    for (uint32_t n = 0; n < 256; ++n)
                    {
                        uint32_t c = n;
                        for (int k = 0; k < 8; ++k)
                        {
                            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                        }
                        table[n] = c;
                    }
                    ready = true;
                }
                uint32_t crc = 0xFFFFFFFFu;
                for (size_t i = 0; i < length; ++i)
                {
                    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
                }
                return ~crc;
            }

            void PutChunk(Bytes& out, const char* type, const Bytes& data)
            {
                PutBigU32(out, static_cast<uint32_t>(data.size()));
                size_t start = out.size();
                out.insert(out.end(), type, type + 4);
                out.insert(out.end(), data.begin(), data.end());
                PutBigU32(out, Crc32(out.data() + start, out.size() - start));
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

        bool EncodePng(const uint8_t* rgb, int width, int height, Bytes& png)
        {
            png.clear();
            if (width <= 0 || height <= 0 || width > kMaxImageDimension || height > kMaxImageDimension)
            {
                return false;
            }
            const size_t rowBytes = static_cast<size_t>(width) * 3;
            Bytes raw;
            raw.reserve((rowBytes + 1) * static_cast<size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                raw.push_back(0); // filter: none
                raw.insert(raw.end(), rgb + static_cast<size_t>(y) * rowBytes,
                           rgb + static_cast<size_t>(y) * rowBytes + rowBytes);
            }
            Bytes packed = ZlibCompress(raw);
            if (packed.empty())
            {
                return false;
            }
            static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            png.insert(png.end(), kSignature, kSignature + 8);
            Bytes header;
            PutBigU32(header, static_cast<uint32_t>(width));
            PutBigU32(header, static_cast<uint32_t>(height));
            header.push_back(8); // bit depth
            header.push_back(2); // colour type: RGB
            header.push_back(0);
            header.push_back(0);
            header.push_back(0);
            PutChunk(png, "IHDR", header);
            PutChunk(png, "IDAT", packed);
            PutChunk(png, "IEND", Bytes());
            return true;
        }

        bool DibToPng(const Bytes& dib, Bytes& png)
        {
            constexpr uint32_t kInfoHeaderSize = 40;
            if (dib.size() < kInfoHeaderSize)
            {
                return false;
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
                width > kMaxImageDimension || height > kMaxImageDimension || (bitCount != 24 && bitCount != 32) ||
                (compression != 0 && compression != 3))
            {
                return false;
            }
            uint32_t redMask = 0x00FF0000, greenMask = 0x0000FF00, blueMask = 0x000000FF;
            size_t offset = headerSize;
            if (compression == 3)
            {
                size_t maskOffset = headerSize > kInfoHeaderSize ? kInfoHeaderSize : headerSize;
                if (maskOffset + 12 > dib.size())
                {
                    return false;
                }
                redMask = ReadU32(data + maskOffset);
                greenMask = ReadU32(data + maskOffset + 4);
                blueMask = ReadU32(data + maskOffset + 8);
                if (headerSize == kInfoHeaderSize)
                {
                    offset += 12;
                }
            }
            offset += static_cast<size_t>(coloursUsed) * 4; // an unused palette sometimes sits before the pixels
            size_t stride = ((static_cast<size_t>(width) * bitCount + 31) / 32) * 4;
            if (offset > dib.size() || stride * static_cast<size_t>(height) > dib.size() - offset)
            {
                return false;
            }
            Bytes rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
            for (int y = 0; y < height; ++y)
            {
                int sourceRow = topDown ? y : height - 1 - y;
                const uint8_t* in = data + offset + stride * static_cast<size_t>(sourceRow);
                uint8_t* out = rgb.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 3;
                for (int x = 0; x < width; ++x)
                {
                    if (bitCount == 24)
                    {
                        out[x * 3 + 0] = in[x * 3 + 2];
                        out[x * 3 + 1] = in[x * 3 + 1];
                        out[x * 3 + 2] = in[x * 3 + 0];
                    }
                    else
                    {
                        uint32_t pixel = ReadU32(in + x * 4);
                        out[x * 3 + 0] = Channel(pixel, redMask);
                        out[x * 3 + 1] = Channel(pixel, greenMask);
                        out[x * 3 + 2] = Channel(pixel, blueMask);
                    }
                }
            }
            return EncodePng(rgb.data(), width, height, png);
        }

        bool PngToDib(const Bytes& png, Bytes& dib)
        {
            dib.clear();
            if (png.size() < 8 || png.size() > 0x7FFFFFFF)
            {
                return false;
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            if (!stbi_info_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels) ||
                width <= 0 || height <= 0 || width > kMaxImageDimension || height > kMaxImageDimension ||
                static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > kMaxImagePixels)
            {
                return false;
            }
            unsigned char* rgba = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height,
                                                        &channels, 4);
            if (rgba == nullptr)
            {
                return false;
            }
            const size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
            dib.reserve(40 + stride * static_cast<size_t>(height));
            PutU32(dib, 40);
            PutU32(dib, static_cast<uint32_t>(width));
            PutU32(dib, static_cast<uint32_t>(height)); // positive: bottom-up
            PutU16(dib, 1);
            PutU16(dib, 24);
            PutU32(dib, 0);
            PutU32(dib, static_cast<uint32_t>(stride * static_cast<size_t>(height)));
            PutU32(dib, 2835);
            PutU32(dib, 2835);
            PutU32(dib, 0);
            PutU32(dib, 0);
            for (int y = height - 1; y >= 0; --y)
            {
                size_t start = dib.size();
                dib.resize(start + stride, 0);
                uint8_t* out = dib.data() + start;
                const unsigned char* row = rgba + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
                for (int x = 0; x < width; ++x)
                {
                    const unsigned char* p = row + x * 4;
                    unsigned alpha = p[3];
                    for (int c = 0; c < 3; ++c)
                    {
                        unsigned value = alpha == 255 ? p[c] : (p[c] * alpha + 255 * (255 - alpha) + 127) / 255;
                        out[x * 3 + (2 - c)] = static_cast<uint8_t>(value);
                    }
                }
            }
            stbi_image_free(rgba);
            return true;
        }
    } // namespace Wire
} // namespace Marconi
