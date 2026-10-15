// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tight decoder: fill, JPEG and zlib-compressed copy/palette/gradient rectangles over four persistent streams

#include <MarconiCore/Rfb/Decoders/Decoders.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(_MSC_VER)
// Unused static stb functions are reported at the end of the file, so this one stays off for the whole file.
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

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            enum : uint8_t
            {
                kFill = 0x08,
                kJpeg = 0x09,
                kExplicitFilter = 0x04,
            };

            enum : uint8_t
            {
                kFilterCopy = 0,
                kFilterPalette = 1,
                kFilterGradient = 2,
            };

            constexpr uint32_t kMaxCompactLength = 64 * 1024 * 1024;

            // TPIXEL for a 32bpp depth-24 client format: three bytes, red green blue.
            uint32_t TPixel(const uint8_t* bytes)
            {
                return (static_cast<uint32_t>(bytes[0]) << 16) | (bytes[1] << 8) | bytes[2];
            }

            uint32_t ReadTPixel(InStream& in)
            {
                uint8_t bytes[3];
                in.Read(bytes, 3);
                return TPixel(bytes);
            }
        } // namespace

        uint32_t ReadCompactLength(InStream& in)
        {
            uint32_t length = 0;
            for (int i = 0; i < 3; ++i)
            {
                uint8_t byte = in.U8();
                length |= static_cast<uint32_t>(i < 2 ? byte & 0x7F : byte) << (7 * i);
                if (i < 2 && (byte & 0x80) == 0)
                {
                    break;
                }
            }
            return length;
        }

        void TightDecoder::Reset()
        {
            for (ZlibStream& stream : m_zlib)
            {
                stream.Reset();
            }
        }

        void TightDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            uint8_t control = in.U8();
            for (int i = 0; i < 4; ++i)
            {
                if (control & (1 << i))
                {
                    m_zlib[i].Reset();
                }
            }

            uint8_t type = control >> 4;
            if (type == kFill)
            {
                uint32_t colour = ReadTPixel(in);
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                framebuffer.Fill(rect, colour);
                return;
            }
            if (type == kJpeg)
            {
                DecodeJpeg(in, rect);
            }
            else if (type <= 7)
            {
                DecodeBasic(in, rect, control);
            }
            else
            {
                throw RfbError("Unsupported Tight compression type " + std::to_string(type));
            }
            m_buffer.CopyTo(framebuffer, rect.x, rect.y);
        }

        Bytes TightDecoder::ReadData(InStream& in, int stream, size_t size)
        {
            // Data shorter than 12 bytes is sent uncompressed.
            if (size < 12)
            {
                return in.ReadBytes(size);
            }
            uint32_t length = ReadCompactLength(in);
            if (length > kMaxCompactLength)
            {
                throw RfbError("Tight rectangle is implausibly large");
            }
            Bytes data = m_zlib[stream].Inflate(in.ReadBytes(length), size);
            if (data.size() < size)
            {
                throw RfbError("Tight rectangle decompressed to too little data");
            }
            return data;
        }

        void TightDecoder::DecodeBasic(InStream& in, const Rect& rect, uint8_t control)
        {
            int stream = (control >> 4) & 3;
            uint8_t filter = (control & (kExplicitFilter << 4)) ? in.U8() : kFilterCopy;
            const size_t pixelCount = static_cast<size_t>(rect.width) * rect.height;
            m_buffer.Resize(rect.width, rect.height);

            if (filter == kFilterPalette)
            {
                int paletteSize = in.U8() + 1;
                uint32_t palette[256];
                for (int i = 0; i < paletteSize; ++i)
                {
                    palette[i] = ReadTPixel(in);
                }
                if (paletteSize == 2)
                {
                    // One bit per pixel, most significant first, rows padded to a byte.
                    size_t rowBytes = (static_cast<size_t>(rect.width) + 7) / 8;
                    Bytes data = ReadData(in, stream, rowBytes * rect.height);
                    for (int y = 0; y < rect.height; ++y)
                    {
                        uint32_t* row = m_buffer.Row(y);
                        const uint8_t* bits = data.data() + y * rowBytes;
                        for (int x = 0; x < rect.width; ++x)
                        {
                            row[x] = palette[(bits[x / 8] >> (7 - x % 8)) & 1];
                        }
                    }
                }
                else
                {
                    Bytes data = ReadData(in, stream, pixelCount);
                    for (size_t i = 0; i < pixelCount; ++i)
                    {
                        if (data[i] >= paletteSize)
                        {
                            throw RfbError("Tight palette index out of range");
                        }
                        m_buffer.pixels[i] = palette[data[i]];
                    }
                }
                return;
            }

            if (filter != kFilterCopy && filter != kFilterGradient)
            {
                throw RfbError("Unknown Tight filter " + std::to_string(filter));
            }

            Bytes data = ReadData(in, stream, pixelCount * 3);
            if (filter == kFilterCopy)
            {
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    m_buffer.pixels[i] = TPixel(data.data() + i * 3);
                }
                return;
            }

            // Gradient: each channel is predicted as left + above - above-left, clamped, and the data holds the
            // difference from the prediction.
            std::vector<uint8_t> previous(static_cast<size_t>(rect.width) * 3, 0);
            std::vector<uint8_t> current(static_cast<size_t>(rect.width) * 3, 0);
            for (int y = 0; y < rect.height; ++y)
            {
                const uint8_t* source = data.data() + static_cast<size_t>(y) * rect.width * 3;
                for (int x = 0; x < rect.width; ++x)
                {
                    for (int c = 0; c < 3; ++c)
                    {
                        int left = x > 0 ? current[(x - 1) * 3 + c] : 0;
                        int above = previous[x * 3 + c];
                        int aboveLeft = x > 0 ? previous[(x - 1) * 3 + c] : 0;
                        int prediction = std::clamp(left + above - aboveLeft, 0, 255);
                        current[x * 3 + c] = static_cast<uint8_t>(prediction + source[x * 3 + c]);
                    }
                    m_buffer.Row(y)[x] = TPixel(current.data() + x * 3);
                }
                previous.swap(current);
            }
        }

        void TightDecoder::DecodeJpeg(InStream& in, const Rect& rect)
        {
            uint32_t length = ReadCompactLength(in);
            if (length > kMaxCompactLength)
            {
                throw RfbError("Tight JPEG rectangle is implausibly large");
            }
            Bytes jpeg = in.ReadBytes(length);

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* rgb = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &width, &height, &channels, 3);
            if (rgb == nullptr)
            {
                throw RfbError(std::string("Could not decode a JPEG rectangle: ") + stbi_failure_reason());
            }
            if (width != rect.width || height != rect.height)
            {
                stbi_image_free(rgb);
                throw RfbError("JPEG rectangle size doesn't match its header");
            }
            m_buffer.Resize(rect.width, rect.height);
            const size_t pixelCount = static_cast<size_t>(width) * height;
            for (size_t i = 0; i < pixelCount; ++i)
            {
                m_buffer.pixels[i] = TPixel(rgb + i * 3);
            }
            stbi_image_free(rgb);
        }
    } // namespace Rfb
} // namespace Marconi
