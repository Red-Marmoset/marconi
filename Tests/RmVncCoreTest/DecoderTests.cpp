// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the encoding decoders, using hand-built encoded data for known images

#include "TestSupport.h"

#include <RmVncCore/Rfb/Decoders/Decoders.h>

#include <zlib.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
// Unused static stb functions are reported at the end of the file, so this one stays off for the whole file.
#pragma warning(disable : 4505)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4456 4457 4996)
#endif
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdlib>

namespace RmVncTest
{
    using namespace RmVnc;

    namespace
    {
        // A deterministic test image: every pixel different.
        uint32_t PatternPixel(int x, int y)
        {
            return (static_cast<uint32_t>(x * 37 + y * 11) & 0xFF) << 16 | (static_cast<uint32_t>(x * 5 + y * 71) & 0xFF) << 8 |
                   (static_cast<uint32_t>(x * 13 + y * 3) & 0xFF);
        }

        void AppendPixel32(Rfb::OutBuffer& out, uint32_t pixel)
        {
            uint8_t bytes[4] = {static_cast<uint8_t>(pixel), static_cast<uint8_t>(pixel >> 8),
                                static_cast<uint8_t>(pixel >> 16), 0};
            out.Append(bytes, 4);
        }

        void AppendCPixel(Rfb::OutBuffer& out, uint32_t pixel) // ZRLE: B G R
        {
            uint8_t bytes[3] = {static_cast<uint8_t>(pixel), static_cast<uint8_t>(pixel >> 8),
                                static_cast<uint8_t>(pixel >> 16)};
            out.Append(bytes, 3);
        }

        void AppendTPixel(Rfb::OutBuffer& out, uint32_t pixel) // Tight: R G B
        {
            uint8_t bytes[3] = {static_cast<uint8_t>(pixel >> 16), static_cast<uint8_t>(pixel >> 8),
                                static_cast<uint8_t>(pixel)};
            out.Append(bytes, 3);
        }

        void AppendCompactLength(Rfb::OutBuffer& out, uint32_t length)
        {
            out.U8(static_cast<uint8_t>((length & 0x7F) | (length > 0x7F ? 0x80 : 0)));
            if (length > 0x7F)
            {
                out.U8(static_cast<uint8_t>(((length >> 7) & 0x7F) | (length > 0x3FFF ? 0x80 : 0)));
                if (length > 0x3FFF)
                {
                    out.U8(static_cast<uint8_t>(length >> 14));
                }
            }
        }

        // A deflate stream that persists across calls, like a VNC server's.
        class Deflater
        {
        public:
            Deflater()
            {
                deflateInit(&m_stream, 6);
            }
            ~Deflater()
            {
                deflateEnd(&m_stream);
            }
            Rfb::Bytes Compress(const Rfb::Bytes& input)
            {
                Rfb::Bytes output(deflateBound(&m_stream, static_cast<uLong>(input.size())) + 64);
                m_stream.next_in = const_cast<Bytef*>(input.data());
                m_stream.avail_in = static_cast<uInt>(input.size());
                m_stream.next_out = output.data();
                m_stream.avail_out = static_cast<uInt>(output.size());
                deflate(&m_stream, Z_SYNC_FLUSH);
                output.resize(output.size() - m_stream.avail_out);
                return output;
            }

        private:
            z_stream m_stream = {};
        };

        // Runs a decoder over `encoded` for `rect` on a fresh framebuffer of the given size, filled with `background`.
        bool DecodeInto(Rfb::Decoder& decoder, const Rfb::Bytes& encoded, const Rfb::Rect& rect, Rfb::Framebuffer& fb)
        {
            Rfb::MemorySource source(encoded);
            Rfb::InStream in(source);
            try
            {
                decoder.Decode(in, rect, fb);
            }
            catch (const std::exception& exception)
            {
                printf("  decode threw: %s\n", exception.what());
                return false;
            }
            // The decoder must consume exactly the rectangle's data.
            return in.GetBytesRead() == encoded.size();
        }

        bool MatchesPattern(Rfb::Framebuffer& fb, const Rfb::Rect& rect, int tolerance = 0)
        {
            for (int y = 0; y < rect.height; ++y)
            {
                for (int x = 0; x < rect.width; ++x)
                {
                    uint32_t expected = PatternPixel(x, y);
                    uint32_t actual = fb.GetPixel(rect.x + x, rect.y + y);
                    for (int shift : {0, 8, 16})
                    {
                        int difference = std::abs(static_cast<int>((expected >> shift) & 0xFF) -
                                                  static_cast<int>((actual >> shift) & 0xFF));
                        if (difference > tolerance || (actual >> 24) != 0)
                        {
                            printf("  pixel (%d,%d) expected %06x got %08x\n", x, y, expected, actual);
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        void JpegWriter(void* context, void* data, int size)
        {
            Rfb::Bytes* out = static_cast<Rfb::Bytes*>(context);
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        }
    } // namespace

    void RunDecoderTests()
    {
        RunTest("CopyRect", []() {
            Rfb::Framebuffer fb;
            fb.Resize(8, 8);
            fb.Row(1)[2] = 0x123456;
            Rfb::OutBuffer data;
            data.U16(2).U16(1);
            Rfb::CopyRectDecoder decoder;
            CHECK(DecodeInto(decoder, data.Data(), {5, 6, 1, 1}, fb));
            CHECK(fb.GetPixel(5, 6) == 0x123456);

            Rfb::OutBuffer bad;
            bad.U16(7).U16(7);
            CHECK(!DecodeInto(decoder, bad.Data(), {0, 0, 2, 2}, fb)); // source outside the desktop
        });

        RunTest("RRE", []() {
            Rfb::OutBuffer data;
            data.U32(2);
            AppendPixel32(data, 0x0000ff);
            AppendPixel32(data, 0xff0000);
            data.U16(1).U16(1).U16(2).U16(1);
            AppendPixel32(data, 0x00ff00);
            data.U16(0).U16(2).U16(1).U16(1);
            Rfb::Framebuffer fb;
            fb.Resize(10, 10);
            Rfb::RreDecoder decoder;
            CHECK(DecodeInto(decoder, data.Data(), {4, 4, 3, 3}, fb));
            CHECK(fb.GetPixel(4, 4) == 0x0000ff && fb.GetPixel(6, 6) == 0x0000ff);
            CHECK(fb.GetPixel(5, 5) == 0xff0000 && fb.GetPixel(6, 5) == 0xff0000);
            CHECK(fb.GetPixel(4, 6) == 0x00ff00);
            CHECK(fb.GetPixel(3, 3) == 0);
        });

        RunTest("Hextile raw, background/foreground and coloured subrects across tiles", []() {
            // 20x18 rectangle: four tiles (16x16, 4x16, 16x2, 4x2).
            Rfb::Rect rect{0, 0, 20, 18};
            Rfb::OutBuffer data;
            // Tile 1: raw pattern.
            data.U8(1);
            for (int y = 0; y < 16; ++y)
            {
                for (int x = 0; x < 16; ++x)
                {
                    AppendPixel32(data, PatternPixel(x, y));
                }
            }
            // Tile 2: background red, foreground white, one foreground subrect at (1,2) size 2x3.
            data.U8(2 | 4 | 8);
            AppendPixel32(data, 0xff0000);
            AppendPixel32(data, 0xffffff);
            data.U8(1).U8((1 << 4) | 2).U8(((2 - 1) << 4) | (3 - 1));
            // Tile 3: no flags: background (red) carries over.
            data.U8(0);
            // Tile 4: coloured subrect.
            data.U8(8 | 16);
            data.U8(1);
            AppendPixel32(data, 0x00ff00);
            data.U8((3 << 4) | 1).U8(0);

            Rfb::Framebuffer fb;
            fb.Resize(20, 18);
            Rfb::HextileDecoder decoder;
            CHECK(DecodeInto(decoder, data.Data(), rect, fb));
            CHECK(MatchesPattern(fb, {0, 0, 16, 16}));
            CHECK(fb.GetPixel(16, 0) == 0xff0000);
            CHECK(fb.GetPixel(17, 2) == 0xffffff && fb.GetPixel(18, 4) == 0xffffff && fb.GetPixel(19, 4) == 0xff0000);
            CHECK(fb.GetPixel(0, 16) == 0xff0000 && fb.GetPixel(15, 17) == 0xff0000);
            CHECK(fb.GetPixel(19, 17) == 0x00ff00 && fb.GetPixel(18, 17) == 0xff0000);
        });

        RunTest("ZRLE raw, solid, packed palette and RLE tiles over one zlib stream", []() {
            Deflater deflater;
            Rfb::ZrleDecoder decoder;
            Rfb::Framebuffer fb;
            fb.Resize(70, 66);

            // Rectangle 1: 70x66 -> tiles 64x64 (raw), 6x64 (solid), 64x2 (packed 2-colour), 6x2 (plain RLE).
            Rfb::OutBuffer tiles;
            tiles.U8(0);
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 64; ++x)
                {
                    AppendCPixel(tiles, PatternPixel(x, y));
                }
            }
            tiles.U8(1);
            AppendCPixel(tiles, 0x112233);
            tiles.U8(2);
            AppendCPixel(tiles, 0x000000);
            AppendCPixel(tiles, 0xffffff);
            for (int row = 0; row < 2; ++row)
            {
                for (int i = 0; i < 8; ++i)
                {
                    tiles.U8(0xAA); // alternating 1,0,1,0,...
                }
            }
            tiles.U8(128);
            AppendCPixel(tiles, 0x0000ff);
            tiles.U8(4); // run of 5
            AppendCPixel(tiles, 0x00ff00);
            tiles.U8(6); // run of 7
            Rfb::Bytes compressed = deflater.Compress(tiles.Data());
            Rfb::OutBuffer data;
            data.U32(static_cast<uint32_t>(compressed.size())).Append(compressed.data(), compressed.size());
            CHECK(DecodeInto(decoder, data.Data(), {0, 0, 70, 66}, fb));
            CHECK(MatchesPattern(fb, {0, 0, 64, 64}));
            CHECK(fb.GetPixel(64, 0) == 0x112233 && fb.GetPixel(69, 63) == 0x112233);
            CHECK(fb.GetPixel(0, 64) == 0xffffff && fb.GetPixel(1, 64) == 0x000000 && fb.GetPixel(63, 65) == 0x000000);
            CHECK(fb.GetPixel(64, 64) == 0x0000ff && fb.GetPixel(68, 64) == 0x0000ff);
            CHECK(fb.GetPixel(69, 64) == 0x00ff00 && fb.GetPixel(69, 65) == 0x00ff00);

            // Rectangle 2 continues the same zlib stream: a palette RLE tile.
            Rfb::OutBuffer tile2;
            tile2.U8(130 + 1); // palette RLE, 3 colours
            AppendCPixel(tile2, 0xaa0000);
            AppendCPixel(tile2, 0x00bb00);
            AppendCPixel(tile2, 0x0000cc);
            tile2.U8(0x80 | 1).U8(2); // colour 1, run of 3
            tile2.U8(2);              // colour 2, single
            Rfb::Bytes compressed2 = deflater.Compress(tile2.Data());
            Rfb::OutBuffer data2;
            data2.U32(static_cast<uint32_t>(compressed2.size())).Append(compressed2.data(), compressed2.size());
            CHECK(DecodeInto(decoder, data2.Data(), {10, 10, 2, 2}, fb));
            CHECK(fb.GetPixel(10, 10) == 0x00bb00 && fb.GetPixel(11, 10) == 0x00bb00 && fb.GetPixel(10, 11) == 0x00bb00);
            CHECK(fb.GetPixel(11, 11) == 0x0000cc);
        });

        RunTest("ZRLE rejects runs that overflow a tile", []() {
            Deflater deflater;
            Rfb::OutBuffer tile;
            tile.U8(128);
            AppendCPixel(tile, 0x0000ff);
            tile.U8(10); // run of 11 in a 2x2 tile
            Rfb::Bytes compressed = deflater.Compress(tile.Data());
            Rfb::OutBuffer data;
            data.U32(static_cast<uint32_t>(compressed.size())).Append(compressed.data(), compressed.size());
            Rfb::Framebuffer fb;
            fb.Resize(4, 4);
            Rfb::ZrleDecoder decoder;
            CHECK(!DecodeInto(decoder, data.Data(), {0, 0, 2, 2}, fb));
        });

        RunTest("Tight compact length", []() {
            for (uint32_t value : {0u, 1u, 127u, 128u, 16383u, 16384u, 4194303u})
            {
                Rfb::OutBuffer out;
                AppendCompactLength(out, value);
                Rfb::MemorySource source(out.Data());
                Rfb::InStream in(source);
                CHECK(Rfb::ReadCompactLength(in) == value);
            }
        });

        RunTest("Tight fill, small uncompressed copy, zlib copy and stream reset", []() {
            Rfb::TightDecoder decoder;
            Rfb::Framebuffer fb;
            fb.Resize(40, 40);

            Rfb::OutBuffer fill;
            fill.U8(0x80);
            AppendTPixel(fill, 0x102030);
            CHECK(DecodeInto(decoder, fill.Data(), {0, 0, 40, 40}, fb));
            CHECK(fb.GetPixel(0, 0) == 0x102030 && fb.GetPixel(39, 39) == 0x102030);

            // 2x1 copy filter: 6 bytes < 12, sent uncompressed.
            Rfb::OutBuffer small;
            small.U8(0x00);
            AppendTPixel(small, PatternPixel(0, 0));
            AppendTPixel(small, PatternPixel(1, 0));
            CHECK(DecodeInto(decoder, small.Data(), {0, 0, 2, 1}, fb));
            CHECK(MatchesPattern(fb, {0, 0, 2, 1}));

            // 32x20 copy filter over zlib stream 1, sent as two rectangles on the same stream.
            Deflater deflater;
            for (int pass = 0; pass < 2; ++pass)
            {
                Rfb::OutBuffer pixels;
                for (int y = 0; y < 20; ++y)
                {
                    for (int x = 0; x < 32; ++x)
                    {
                        AppendTPixel(pixels, PatternPixel(x, y));
                    }
                }
                Rfb::Bytes compressed = deflater.Compress(pixels.Data());
                Rfb::OutBuffer data;
                data.U8(0x10); // stream 1, no explicit filter
                AppendCompactLength(data, static_cast<uint32_t>(compressed.size()));
                data.Append(compressed.data(), compressed.size());
                fb.Fill({0, 0, 40, 40}, 0);
                CHECK(DecodeInto(decoder, data.Data(), {4, 8, 32, 20}, fb));
                CHECK(MatchesPattern(fb, {4, 8, 32, 20}));
            }

            // Reset bit for stream 1 plus a brand new zlib stream.
            Deflater fresh;
            Rfb::OutBuffer pixels;
            for (int i = 0; i < 16; ++i)
            {
                AppendTPixel(pixels, 0x445566);
            }
            Rfb::Bytes compressed = fresh.Compress(pixels.Data());
            Rfb::OutBuffer data;
            data.U8(0x12); // reset stream 1, use stream 1
            AppendCompactLength(data, static_cast<uint32_t>(compressed.size()));
            data.Append(compressed.data(), compressed.size());
            CHECK(DecodeInto(decoder, data.Data(), {0, 0, 4, 4}, fb));
            CHECK(fb.GetPixel(3, 3) == 0x445566);
        });

        RunTest("Tight palette filters (2 colour bitmap and 8-bit indices)", []() {
            Rfb::TightDecoder decoder;
            Rfb::Framebuffer fb;
            fb.Resize(20, 4);

            // 10x2, two colours: 2 bytes per row = 4 bytes < 12, uncompressed.
            Rfb::OutBuffer twoColour;
            twoColour.U8(0x40).U8(1).U8(1); // explicit filter, palette, 2 colours
            AppendTPixel(twoColour, 0x000000);
            AppendTPixel(twoColour, 0xffffff);
            twoColour.U8(0xA0).U8(0x40).U8(0x00).U8(0xC0); // row 0: 1010000001, row 1: 0000000011
            CHECK(DecodeInto(decoder, twoColour.Data(), {0, 0, 10, 2}, fb));
            CHECK(fb.GetPixel(0, 0) == 0xffffff && fb.GetPixel(1, 0) == 0 && fb.GetPixel(2, 0) == 0xffffff);
            CHECK(fb.GetPixel(9, 0) == 0xffffff && fb.GetPixel(8, 0) == 0);
            CHECK(fb.GetPixel(8, 1) == 0xffffff && fb.GetPixel(9, 1) == 0xffffff && fb.GetPixel(0, 1) == 0);

            // 4x4, three colours: 16 index bytes, compressed on stream 2.
            Deflater deflater;
            Rfb::Bytes indices;
            for (int i = 0; i < 16; ++i)
            {
                indices.push_back(static_cast<uint8_t>(i % 3));
            }
            Rfb::Bytes compressed = deflater.Compress(indices);
            Rfb::OutBuffer threeColour;
            threeColour.U8(0x60).U8(1).U8(2); // stream 2 + explicit filter, palette, 3 colours
            AppendTPixel(threeColour, 0xff0000);
            AppendTPixel(threeColour, 0x00ff00);
            AppendTPixel(threeColour, 0x0000ff);
            AppendCompactLength(threeColour, static_cast<uint32_t>(compressed.size()));
            threeColour.Append(compressed.data(), compressed.size());
            CHECK(DecodeInto(decoder, threeColour.Data(), {12, 0, 4, 4}, fb));
            CHECK(fb.GetPixel(12, 0) == 0xff0000 && fb.GetPixel(13, 0) == 0x00ff00 && fb.GetPixel(14, 0) == 0x0000ff);
            CHECK(fb.GetPixel(15, 3) == 0xff0000); // index 15 % 3 = 0
        });

        RunTest("Tight gradient filter", []() {
            // Encode the pattern with the gradient predictor, independently of the decoder's implementation.
            const int width = 9;
            const int height = 5;
            auto channel = [](int x, int y, int c) -> int {
                if (x < 0 || y < 0)
                {
                    return 0;
                }
                return static_cast<int>((PatternPixel(x, y) >> (16 - 8 * c)) & 0xFF);
            };
            Rfb::Bytes residuals;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    for (int c = 0; c < 3; ++c)
                    {
                        int prediction = channel(x - 1, y, c) + channel(x, y - 1, c) - channel(x - 1, y - 1, c);
                        prediction = prediction < 0 ? 0 : (prediction > 255 ? 255 : prediction);
                        residuals.push_back(static_cast<uint8_t>(channel(x, y, c) - prediction));
                    }
                }
            }
            Deflater deflater;
            Rfb::Bytes compressed = deflater.Compress(residuals);
            Rfb::OutBuffer data;
            data.U8(0x40).U8(2); // explicit filter: gradient
            AppendCompactLength(data, static_cast<uint32_t>(compressed.size()));
            data.Append(compressed.data(), compressed.size());

            Rfb::TightDecoder decoder;
            Rfb::Framebuffer fb;
            fb.Resize(width, height);
            CHECK(DecodeInto(decoder, data.Data(), {0, 0, width, height}, fb));
            CHECK(MatchesPattern(fb, {0, 0, width, height}));
        });

        RunTest("Tight JPEG", []() {
            const int width = 24;
            const int height = 16;
            // Smooth image so JPEG at high quality stays within a small tolerance.
            std::vector<uint8_t> rgb;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    rgb.push_back(static_cast<uint8_t>(40 + x * 4));
                    rgb.push_back(static_cast<uint8_t>(90 + y * 4));
                    rgb.push_back(160);
                }
            }
            Rfb::Bytes jpeg;
            CHECK(stbi_write_jpg_to_func(JpegWriter, &jpeg, width, height, 3, rgb.data(), 100) != 0);

            Rfb::OutBuffer data;
            data.U8(0x90);
            AppendCompactLength(data, static_cast<uint32_t>(jpeg.size()));
            data.Append(jpeg.data(), jpeg.size());

            Rfb::TightDecoder decoder;
            Rfb::Framebuffer fb;
            fb.Resize(width, height);
            CHECK(DecodeInto(decoder, data.Data(), {0, 0, width, height}, fb));
            int worst = 0;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint32_t pixel = fb.GetPixel(x, y);
                    const uint8_t* expected = rgb.data() + (y * width + x) * 3;
                    worst = std::max({worst, std::abs(static_cast<int>(pixel >> 16 & 0xFF) - expected[0]),
                                      std::abs(static_cast<int>(pixel >> 8 & 0xFF) - expected[1]),
                                      std::abs(static_cast<int>(pixel & 0xFF) - expected[2])});
                }
            }
            CHECK(worst <= 6);

            // A JPEG whose size doesn't match the rectangle is an error.
            Rfb::Framebuffer big;
            big.Resize(64, 64);
            CHECK(!DecodeInto(decoder, data.Data(), {0, 0, 32, 16}, big));
        });
    }
} // namespace RmVncTest
