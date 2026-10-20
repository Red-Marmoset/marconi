// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's classic bitmap updates: uncompressed rectangles and the interleaved run-length encoding, decoded into
// top-down 0x00RRGGBB pixels. The decoder follows the specification's reference algorithm (MS-RDPBCGR 3.1.9): it works
// on pixels in the stream's own format, so the XORs against the row below mean the same as on the server, and
// converts to RGB only at the end.

#include <MarconiCore/Rdp/Bitmap.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr size_t kMaxPixels = 16 * 1024 * 1024; // far above any real update rectangle

            // Order codes (MS-RDPBCGR 2.2.9.1.1.3.1.2.4). Regular orders keep their code in the top 3 bits, "lite"
            // orders in the top 4, and the mega-mega and special orders use the whole byte.
            enum : uint8_t
            {
                kRegularBgRun = 0x0,
                kRegularFgRun = 0x1,
                kRegularFgBgImage = 0x2,
                kRegularColorRun = 0x3,
                kRegularColorImage = 0x4,
                kLiteSetFgFgRun = 0xC,
                kLiteSetFgFgBgImage = 0xD,
                kLiteDitheredRun = 0xE,
                kMegaMegaBgRun = 0xF0,
                kMegaMegaFgRun = 0xF1,
                kMegaMegaFgBgImage = 0xF2,
                kMegaMegaColorRun = 0xF3,
                kMegaMegaColorImage = 0xF4,
                kMegaMegaSetFgRun = 0xF6,
                kMegaMegaSetFgBgImage = 0xF7,
                kMegaMegaDitheredRun = 0xF8,
                kSpecialFgBg1 = 0xF9,
                kSpecialFgBg2 = 0xFA,
                kSpecialWhite = 0xFD,
                kSpecialBlack = 0xFE,
            };
            constexpr uint8_t kMaskSpecialFgBg1 = 0x03;
            constexpr uint8_t kMaskSpecialFgBg2 = 0x05;

            uint8_t CodeOf(uint8_t header)
            {
                if ((header & 0xC0) != 0xC0)
                {
                    return header >> 5; // regular
                }
                if ((header & 0xF0) == 0xF0)
                {
                    return header; // mega-mega or special
                }
                return header >> 4; // lite
            }

            int BytesPerPixel(int bitsPerPixel)
            {
                switch (bitsPerPixel)
                {
                    case 8: return 1;
                    case 15:
                    case 16: return 2;
                    case 24: return 3;
                    default: return 0;
                }
            }

            uint32_t WhiteFor(int bitsPerPixel)
            {
                switch (bitsPerPixel)
                {
                    case 8: return 0xFF;
                    case 15: return 0x7FFF;
                    case 16: return 0xFFFF;
                    default: return 0xFFFFFF;
                }
            }

            // Native pixel to 0x00RRGGBB. 15 and 16 bit values have their 5 and 6 bit channels widened so that full
            // intensity is 0xFF.
            uint32_t ToRgb(uint32_t pixel, int bitsPerPixel, const uint32_t* palette)
            {
                switch (bitsPerPixel)
                {
                    case 8: return palette[pixel & 0xFF] & 0x00FFFFFF;
                    case 15:
                    {
                        uint32_t r = (pixel >> 10) & 0x1F, g = (pixel >> 5) & 0x1F, b = pixel & 0x1F;
                        return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
                    }
                    case 16:
                    {
                        uint32_t r = (pixel >> 11) & 0x1F, g = (pixel >> 5) & 0x3F, b = pixel & 0x1F;
                        return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
                    }
                    default: return pixel & 0x00FFFFFF; // 24-bit pixels arrive as B, G, R: already 0x00RRGGBB
                }
            }

            bool CheckArguments(int width, int height, int bitsPerPixel, const uint32_t* palette, std::string& error)
            {
                if (width <= 0 || height <= 0 || static_cast<size_t>(width) * static_cast<size_t>(height) > kMaxPixels)
                {
                    error = "The server sent a bitmap with an impossible size";
                    return false;
                }
                if (BytesPerPixel(bitsPerPixel) == 0)
                {
                    error = "The server sent a bitmap in a colour depth Marconi doesn't decode (" +
                            std::to_string(bitsPerPixel) + " bits)";
                    return false;
                }
                if (bitsPerPixel == 8 && palette == nullptr)
                {
                    error = "The server sent an 8-bit bitmap before its colour palette";
                    return false;
                }
                return true;
            }

            // Native pixels in decode order (bottom row first) to top-down RGB.
            void FlipAndConvert(const std::vector<uint32_t>& native, int width, int height, int bitsPerPixel,
                                const uint32_t* palette, std::vector<uint32_t>& out)
            {
                out.assign(native.size(), 0);
                for (int row = 0; row < height; ++row)
                {
                    const uint32_t* from = native.data() + static_cast<size_t>(row) * width;
                    uint32_t* to = out.data() + static_cast<size_t>(height - 1 - row) * width;
                    for (int x = 0; x < width; ++x)
                    {
                        to[x] = ToRgb(from[x], bitsPerPixel, palette);
                    }
                }
            }

            // The run-length decoder's state: the source stream and the destination, both bounds-checked.
            class RleDecoder
            {
            public:
                RleDecoder(const uint8_t* data, size_t length, int width, int height, int bitsPerPixel)
                    : m_src(data), m_end(data + length), m_width(static_cast<size_t>(width)),
                      m_bytesPerPixel(BytesPerPixel(bitsPerPixel)), m_white(WhiteFor(bitsPerPixel)),
                      m_pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0)
                {
                }

                bool Run(std::string& error);
                const std::vector<uint32_t>& GetPixels() const { return m_pixels; }

            private:
                bool ReadByte(uint8_t& value)
                {
                    if (m_src >= m_end)
                    {
                        return Fail("The server's compressed bitmap ended in the middle of an instruction");
                    }
                    value = *m_src++;
                    return true;
                }

                bool ReadPixel(uint32_t& value)
                {
                    if (static_cast<size_t>(m_end - m_src) < static_cast<size_t>(m_bytesPerPixel))
                    {
                        return Fail("The server's compressed bitmap ended in the middle of a pixel");
                    }
                    value = 0;
                    for (int i = 0; i < m_bytesPerPixel; ++i)
                    {
                        value |= static_cast<uint32_t>(*m_src++) << (8 * i); // little-endian
                    }
                    return true;
                }

                bool Write(uint32_t value)
                {
                    if (m_dest >= m_pixels.size())
                    {
                        return Fail("The server's compressed bitmap runs past the edge of its rectangle");
                    }
                    m_pixels[m_dest++] = value;
                    return true;
                }

                // The pixel one row up in the picture (one row back in decode order); only called off the first row.
                uint32_t Above() const { return m_pixels[m_dest - m_width]; }

                bool Fail(const char* message)
                {
                    m_error = message;
                    return false;
                }

                bool ReadRunLength(uint8_t header, uint8_t code, uint32_t& length);
                bool WriteFgBgImage(uint8_t bitmask, int bits);

                const uint8_t* m_src;
                const uint8_t* m_end;
                size_t m_width;
                int m_bytesPerPixel;
                uint32_t m_white;
                std::vector<uint32_t> m_pixels;
                size_t m_dest = 0;
                bool m_firstLine = true;
                uint32_t m_foreground = 0;
                std::string m_error;
            };

            bool RleDecoder::ReadRunLength(uint8_t header, uint8_t code, uint32_t& length)
            {
                uint8_t extra = 0;
                switch (code)
                {
                    case kRegularFgBgImage:
                        length = header & 0x1F;
                        if (length == 0)
                        {
                            if (!ReadByte(extra))
                            {
                                return false;
                            }
                            length = extra + 1u;
                        }
                        else
                        {
                            length *= 8;
                        }
                        return true;
                    case kLiteSetFgFgBgImage:
                        length = header & 0x0F;
                        if (length == 0)
                        {
                            if (!ReadByte(extra))
                            {
                                return false;
                            }
                            length = extra + 1u;
                        }
                        else
                        {
                            length *= 8;
                        }
                        return true;
                    case kRegularBgRun:
                    case kRegularFgRun:
                    case kRegularColorRun:
                    case kRegularColorImage:
                        length = header & 0x1F;
                        if (length == 0)
                        {
                            if (!ReadByte(extra))
                            {
                                return false;
                            }
                            length = extra + 32u;
                        }
                        return true;
                    case kLiteSetFgFgRun:
                    case kLiteDitheredRun:
                        length = header & 0x0F;
                        if (length == 0)
                        {
                            if (!ReadByte(extra))
                            {
                                return false;
                            }
                            length = extra + 16u;
                        }
                        return true;
                    default: // the mega-mega orders: a 16-bit little-endian length follows
                    {
                        uint8_t low = 0;
                        uint8_t high = 0;
                        if (!ReadByte(low) || !ReadByte(high))
                        {
                            return false;
                        }
                        length = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 8);
                        return true;
                    }
                }
            }

            // Up to 8 pixels from a bitmask, lowest bit first: a set bit is the foreground colour (XORed with the
            // pixel above, off the first row), a clear bit is black (or the pixel above).
            bool RleDecoder::WriteFgBgImage(uint8_t bitmask, int bits)
            {
                for (int i = 0; i < bits; ++i)
                {
                    bool set = (bitmask & (1u << i)) != 0;
                    uint32_t pixel = m_firstLine ? (set ? m_foreground : 0) : (set ? Above() ^ m_foreground : Above());
                    if (!Write(pixel))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool RleDecoder::Run(std::string& error)
            {
                m_foreground = m_white;
                bool insertForeground = false;
                while (m_src < m_end)
                {
                    // Whether an instruction counts as on the first row is decided as it starts, as the
                    // specification's algorithm does.
                    if (m_firstLine && m_dest >= m_width)
                    {
                        m_firstLine = false;
                        insertForeground = false;
                    }
                    uint8_t header = *m_src++;
                    uint8_t code = CodeOf(header);
                    uint32_t length = 0;

                    if (code == kRegularBgRun || code == kMegaMegaBgRun)
                    {
                        if (!ReadRunLength(header, code, length))
                        {
                            break;
                        }
                        // Two background runs in a row have a foreground pixel between them.
                        if (insertForeground && length > 0)
                        {
                            if (!Write(m_firstLine ? m_foreground : Above() ^ m_foreground))
                            {
                                break;
                            }
                            --length;
                        }
                        bool ok = true;
                        for (; length > 0 && ok; --length)
                        {
                            ok = Write(m_firstLine ? 0 : Above());
                        }
                        if (!ok)
                        {
                            break;
                        }
                        insertForeground = true;
                        continue;
                    }
                    insertForeground = false;

                    bool ok = true;
                    switch (code)
                    {
                        case kRegularFgRun:
                        case kMegaMegaFgRun:
                        case kLiteSetFgFgRun:
                        case kMegaMegaSetFgRun:
                            ok = ReadRunLength(header, code, length);
                            if (ok && (code == kLiteSetFgFgRun || code == kMegaMegaSetFgRun))
                            {
                                ok = ReadPixel(m_foreground);
                            }
                            for (; ok && length > 0; --length)
                            {
                                ok = Write(m_firstLine ? m_foreground : Above() ^ m_foreground);
                            }
                            break;
                        case kLiteDitheredRun:
                        case kMegaMegaDitheredRun:
                        {
                            uint32_t a = 0;
                            uint32_t b = 0;
                            ok = ReadRunLength(header, code, length) && ReadPixel(a) && ReadPixel(b);
                            for (; ok && length > 0; --length)
                            {
                                ok = Write(a) && Write(b);
                            }
                            break;
                        }
                        case kRegularColorRun:
                        case kMegaMegaColorRun:
                        {
                            uint32_t colour = 0;
                            ok = ReadRunLength(header, code, length) && ReadPixel(colour);
                            for (; ok && length > 0; --length)
                            {
                                ok = Write(colour);
                            }
                            break;
                        }
                        case kRegularFgBgImage:
                        case kMegaMegaFgBgImage:
                        case kLiteSetFgFgBgImage:
                        case kMegaMegaSetFgBgImage:
                            ok = ReadRunLength(header, code, length);
                            if (ok && (code == kLiteSetFgFgBgImage || code == kMegaMegaSetFgBgImage))
                            {
                                ok = ReadPixel(m_foreground);
                            }
                            while (ok && length > 0)
                            {
                                uint8_t bitmask = 0;
                                int bits = length > 8 ? 8 : static_cast<int>(length);
                                ok = ReadByte(bitmask) && WriteFgBgImage(bitmask, bits);
                                length -= static_cast<uint32_t>(bits);
                            }
                            break;
                        case kRegularColorImage:
                        case kMegaMegaColorImage:
                            ok = ReadRunLength(header, code, length);
                            for (; ok && length > 0; --length)
                            {
                                uint32_t pixel = 0;
                                ok = ReadPixel(pixel) && Write(pixel);
                            }
                            break;
                        case kSpecialFgBg1:
                            ok = WriteFgBgImage(kMaskSpecialFgBg1, 8);
                            break;
                        case kSpecialFgBg2:
                            ok = WriteFgBgImage(kMaskSpecialFgBg2, 8);
                            break;
                        case kSpecialWhite:
                            ok = Write(m_white);
                            break;
                        case kSpecialBlack:
                            ok = Write(0);
                            break;
                        default:
                            ok = Fail("The server's compressed bitmap has an instruction Marconi doesn't recognise");
                            break;
                    }
                    if (!ok)
                    {
                        break;
                    }
                }
                if (!m_error.empty())
                {
                    error = m_error;
                    return false;
                }
                return true; // a stream that stops short leaves the rest black, as other clients do
            }
        } // namespace

        bool DecodeInterleavedRle(const uint8_t* data, size_t length, int width, int height, int bitsPerPixel,
                                  const uint32_t* palette, std::vector<uint32_t>& out, std::string& error)
        {
            if (!CheckArguments(width, height, bitsPerPixel, palette, error))
            {
                return false;
            }
            RleDecoder decoder(data, length, width, height, bitsPerPixel);
            if (!decoder.Run(error))
            {
                return false;
            }
            FlipAndConvert(decoder.GetPixels(), width, height, bitsPerPixel, palette, out);
            return true;
        }

        bool DecodeUncompressedBitmap(const uint8_t* data, size_t length, int width, int height, int bitsPerPixel,
                                      const uint32_t* palette, std::vector<uint32_t>& out, std::string& error)
        {
            if (!CheckArguments(width, height, bitsPerPixel, palette, error))
            {
                return false;
            }
            const size_t bytesPerPixel = static_cast<size_t>(BytesPerPixel(bitsPerPixel));
            const size_t tight = static_cast<size_t>(width) * bytesPerPixel;
            const size_t padded = (tight + 3) & ~static_cast<size_t>(3);
            size_t stride = 0;
            if (length == padded * static_cast<size_t>(height))
            {
                stride = padded;
            }
            else if (length == tight * static_cast<size_t>(height))
            {
                stride = tight;
            }
            else
            {
                error = "The server sent an uncompressed bitmap of the wrong length";
                return false;
            }
            std::vector<uint32_t> native(static_cast<size_t>(width) * static_cast<size_t>(height));
            for (int row = 0; row < height; ++row)
            {
                const uint8_t* line = data + static_cast<size_t>(row) * stride;
                for (int x = 0; x < width; ++x)
                {
                    uint32_t pixel = 0;
                    for (size_t i = 0; i < bytesPerPixel; ++i)
                    {
                        pixel |= static_cast<uint32_t>(line[static_cast<size_t>(x) * bytesPerPixel + i]) << (8 * i);
                    }
                    native[static_cast<size_t>(row) * width + x] = pixel;
                }
            }
            FlipAndConvert(native, width, height, bitsPerPixel, palette, out);
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
