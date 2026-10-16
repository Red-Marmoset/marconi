// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ZRLE decoder: zlib-compressed 64x64 tiles of raw, solid, packed-palette or run-length data (RFC 6143 7.7.6)

#include <MarconiCore/Rfb/Decoders/Decoders.h>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kMaxCompressedLength = 64 * 1024 * 1024;

            // CPIXEL for our 32bpp depth-24 little-endian client format: the three low-order bytes, B G R.
            uint32_t ReadCPixel(InStream& in)
            {
                uint8_t bytes[3];
                in.Read(bytes, 3);
                return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
            }

            uint32_t ReadRunLength(InStream& in)
            {
                uint32_t length = 1;
                uint8_t byte;
                do
                {
                    byte = in.U8();
                    length += byte;
                } while (byte == 255);
                return length;
            }

            void DecodeTile(InStream& in, PixelBuffer& buffer, int tileX, int tileY, int width, int height)
            {
                uint8_t subencoding = in.U8();
                const size_t tilePixels = static_cast<size_t>(width) * height;

                if (subencoding == 0)
                {
                    for (int y = 0; y < height; ++y)
                    {
                        uint32_t* row = buffer.Row(tileY + y) + tileX;
                        for (int x = 0; x < width; ++x)
                        {
                            row[x] = ReadCPixel(in);
                        }
                    }
                    return;
                }
                if (subencoding == 1)
                {
                    buffer.Fill(tileX, tileY, width, height, ReadCPixel(in));
                    return;
                }

                uint32_t palette[128];
                int paletteSize = 0;
                if (subencoding >= 2 && subencoding <= 16)
                {
                    paletteSize = subencoding;
                }
                else if (subencoding >= 130)
                {
                    paletteSize = subencoding - 128;
                }
                else if (subencoding != 128)
                {
                    throw RfbError("Unknown ZRLE subencoding " + std::to_string(subencoding));
                }
                for (int i = 0; i < paletteSize; ++i)
                {
                    palette[i] = ReadCPixel(in);
                }

                if (subencoding <= 16)
                {
                    // Packed palette: 1, 2 or 4 bits per index, each row padded to a whole byte.
                    int bits = paletteSize == 2 ? 1 : (paletteSize <= 4 ? 2 : 4);
                    uint8_t mask = static_cast<uint8_t>((1 << bits) - 1);
                    for (int y = 0; y < height; ++y)
                    {
                        uint32_t* row = buffer.Row(tileY + y) + tileX;
                        int shift = 0;
                        uint8_t byte = 0;
                        for (int x = 0; x < width; ++x)
                        {
                            if (shift == 0)
                            {
                                byte = in.U8();
                                shift = 8;
                            }
                            shift -= bits;
                            int index = (byte >> shift) & mask;
                            if (index >= paletteSize)
                            {
                                throw RfbError("ZRLE palette index out of range");
                            }
                            row[x] = palette[index];
                        }
                    }
                    return;
                }

                // Run-length encoded, plain (128) or with a palette (130-255), filling the tile left to right.
                size_t position = 0;
                while (position < tilePixels)
                {
                    uint32_t colour;
                    uint32_t length = 1;
                    if (subencoding == 128)
                    {
                        colour = ReadCPixel(in);
                        length = ReadRunLength(in);
                    }
                    else
                    {
                        uint8_t index = in.U8();
                        if ((index & 0x7F) >= paletteSize)
                        {
                            throw RfbError("ZRLE palette index out of range");
                        }
                        colour = palette[index & 0x7F];
                        if (index & 0x80)
                        {
                            length = ReadRunLength(in);
                        }
                    }
                    if (length > tilePixels - position)
                    {
                        throw RfbError("ZRLE run overflows its tile");
                    }
                    for (uint32_t i = 0; i < length; ++i, ++position)
                    {
                        buffer.Row(tileY + static_cast<int>(position / width))[tileX + position % width] = colour;
                    }
                }
            }
        } // namespace

        void ZrleDecoder::Decode(InStream& in, const Rect& rect, Framebuffer& framebuffer)
        {
            uint32_t length = in.U32();
            if (length > kMaxCompressedLength)
            {
                throw RfbError("ZRLE rectangle is implausibly large");
            }
            Bytes compressed = in.ReadBytes(length);
            MemorySource source(m_zlib.Inflate(compressed, static_cast<size_t>(rect.width) * rect.height * 3),
                                "ZRLE rectangle decompressed to too little data");
            InStream tiles(source);

            m_buffer.Resize(rect.width, rect.height);
            for (int tileY = 0; tileY < rect.height; tileY += 64)
            {
                for (int tileX = 0; tileX < rect.width; tileX += 64)
                {
                    DecodeTile(tiles, m_buffer, tileX, tileY, std::min(64, rect.width - tileX),
                               std::min(64, rect.height - tileY));
                }
            }
            m_buffer.CopyTo(framebuffer, rect.x, rect.y);
        }
    } // namespace Rfb
} // namespace Marconi
