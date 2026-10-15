// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for InStream / OutBuffer / Framebuffer

#include "TestSupport.h"

#include <MarconiCore/Rfb/Framebuffer.h>
#include <MarconiCore/Rfb/Messages.h>

namespace MarconiTest
{
    using namespace Marconi;

    void RunStreamTests()
    {
        RunTest("InStream reads big-endian values and strings", []() {
            Rfb::MemorySource source(HexBytes("01 0203 04050607 fffffffe 41424344 aabbccdd 11223300"));
            Rfb::InStream in(source);
            CHECK(in.U8() == 0x01);
            CHECK(in.U16() == 0x0203);
            CHECK(in.U32() == 0x04050607u);
            CHECK(in.S32() == -2);
            CHECK(in.ReadString(4) == "ABCD");
            CHECK(in.Pixel32() == 0xddccbbaau);
            in.Skip(2);
            CHECK(in.U8() == 0x33);
            CHECK(in.GetBytesRead() == 22);

            bool threw = false;
            try
            {
                in.U16(); // only one byte left
            }
            catch (const Rfb::RfbError&)
            {
                threw = true;
            }
            CHECK(threw);
        });

        RunTest("InStream handles reads larger than its buffer", []() {
            Rfb::Bytes data(200000);
            for (size_t i = 0; i < data.size(); ++i)
            {
                data[i] = static_cast<uint8_t>(i * 7);
            }
            Rfb::MemorySource source(data);
            Rfb::InStream in(source);
            CHECK(in.U8() == 0);
            Rfb::Bytes rest = in.ReadBytes(data.size() - 1);
            CHECK(std::equal(rest.begin(), rest.end(), data.begin() + 1));
        });

        RunTest("OutBuffer writes big-endian", []() {
            Rfb::OutBuffer out;
            out.U8(1).U16(0x0203).U32(0x04050607).S32(-1).Padding(2).Append("hi");
            CHECK(out.Data() == HexBytes("01 0203 04050607 ffffffff 0000 6869"));
        });

        RunTest("PixelFormat round trip", []() {
            Rfb::PixelFormat format;
            format.bitsPerPixel = 16;
            format.depth = 16;
            format.bigEndian = true;
            format.redMax = 31;
            format.greenMax = 63;
            format.blueMax = 31;
            format.redShift = 11;
            format.greenShift = 5;
            format.blueShift = 0;
            Rfb::OutBuffer out;
            format.Write(out);
            CHECK(out.Data().size() == 16);
            Rfb::MemorySource source(out.Data());
            Rfb::InStream in(source);
            Rfb::PixelFormat read = Rfb::PixelFormat::Read(in);
            CHECK(read.bitsPerPixel == 16 && read.depth == 16 && read.bigEndian && read.trueColour);
            CHECK(read.redMax == 31 && read.greenMax == 63 && read.blueMax == 31);
            CHECK(read.redShift == 11 && read.greenShift == 5 && read.blueShift == 0);
        });

        RunTest("Framebuffer fill, put, overlapping copy and resize", []() {
            Rfb::Framebuffer fb;
            fb.Resize(4, 4);
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    fb.Row(y)[x] = static_cast<uint32_t>(y * 4 + x);
                }
            }
            // Copy the top-left 3x3 one pixel down-right (overlapping): must behave like a copy via a temp buffer.
            fb.CopyRect({1, 1, 3, 3}, 0, 0);
            CHECK(fb.GetPixel(1, 1) == 0 && fb.GetPixel(3, 3) == 10 && fb.GetPixel(2, 1) == 1 && fb.GetPixel(1, 3) == 8);
            // And up-left.
            fb.CopyRect({0, 0, 3, 3}, 1, 1);
            CHECK(fb.GetPixel(0, 0) == 0 && fb.GetPixel(2, 2) == 10);

            fb.Fill({1, 0, 2, 1}, 0xff);
            CHECK(fb.GetPixel(0, 0) == 0 && fb.GetPixel(1, 0) == 0xff && fb.GetPixel(2, 0) == 0xff && fb.GetPixel(3, 0) == 3);

            uint32_t pixels[] = {7, 8, 9, 10};
            fb.PutPixels({2, 2, 2, 2}, pixels, 2);
            CHECK(fb.GetPixel(2, 2) == 7 && fb.GetPixel(3, 3) == 10);

            CHECK(fb.Contains({0, 0, 4, 4}));
            CHECK(!fb.Contains({1, 0, 4, 1}));
            CHECK(!fb.Contains({-1, 0, 1, 1}));

            fb.Resize(5, 2);
            CHECK(fb.GetWidth() == 5 && fb.GetHeight() == 2);
            CHECK(fb.GetPixel(1, 0) == 0xff && fb.GetPixel(4, 1) == 0);
        });

        RunTest("Rect union", []() {
            Rfb::Rect a{0, 0, 2, 2};
            Rfb::Rect b{5, 1, 1, 4};
            Rfb::Rect u = a.Union(b);
            CHECK(u.x == 0 && u.y == 0 && u.width == 6 && u.height == 5);
            CHECK(Rfb::Rect().Union(b).x == 5);
        });
    }
} // namespace MarconiTest
