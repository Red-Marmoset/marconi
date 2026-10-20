// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP's classic bitmap decoder (interleaved RLE and uncompressed). The streams are built by hand from the
// order encodings in MS-RDPBCGR 2.2.9.1.1.3.1.2.4, with the expected pixels worked out from its decoding algorithm.

#include "TestSupport.h"

#include <MarconiCore/Rdp/Bitmap.h>

#include <string>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        using Pixels = std::vector<uint32_t>;
        constexpr uint32_t kWhite = 0xFFFFFF;

        // Decodes a 24-bit stream; empty on failure.
        Pixels Rle24(const std::vector<uint8_t>& stream, int width, int height)
        {
            Pixels out;
            std::string error;
            if (!Rdp::DecodeInterleavedRle(stream.data(), stream.size(), width, height, 24, nullptr, out, error))
            {
                printf("  decode failed: %s\n", error.c_str());
                return {};
            }
            return out;
        }

        // A 24-bit pixel as it appears in the stream: blue, green, red.
        void Put24(std::vector<uint8_t>& stream, uint32_t rgb)
        {
            stream.push_back(static_cast<uint8_t>(rgb & 0xFF));
            stream.push_back(static_cast<uint8_t>((rgb >> 8) & 0xFF));
            stream.push_back(static_cast<uint8_t>((rgb >> 16) & 0xFF));
        }
    } // namespace

    void RunRdpBitmapTests()
    {
        RunTest("RLE: a colour run fills its length with one colour", []() {
            std::vector<uint8_t> s = {0x64}; // regular colour run, length 4
            Put24(s, 0x112233);
            CHECK(Rle24(s, 4, 1) == Pixels(4, 0x112233));
        });

        RunTest("RLE: a colour image copies its pixels as they are", []() {
            std::vector<uint8_t> s = {0x82}; // regular colour image, 2 pixels
            Put24(s, 0x010203);
            Put24(s, 0x0A0B0C);
            CHECK(Rle24(s, 2, 1) == (Pixels{0x010203, 0x0A0B0C}));
        });

        RunTest("RLE: on the first row a background run is black and a foreground run is white", []() {
            CHECK(Rle24({0x02, 0x22}, 4, 1) == (Pixels{0, 0, kWhite, kWhite}));
        });

        RunTest("RLE: two background runs in a row have a foreground pixel between them", []() {
            CHECK(Rle24({0x02, 0x02}, 4, 1) == (Pixels{0, 0, kWhite, 0}));
            // Anything else in between cancels the inserted pixel.
            CHECK(Rle24({0x01, 0xFE, 0x02}, 4, 1) == (Pixels{0, 0, 0, 0}));
        });

        RunTest("RLE: rows arrive bottom first, and later rows XOR against the row below", []() {
            std::vector<uint8_t> s = {0x82}; // bottom row: two pixels as they are
            Put24(s, 0x010203);
            Put24(s, 0x0A0B0C);
            s.push_back(0x22); // top row: a foreground run of 2, each pixel the one below XOR white
            Pixels expected = {0x010203 ^ kWhite, 0x0A0B0C ^ kWhite, 0x010203, 0x0A0B0C};
            CHECK(Rle24(s, 2, 2) == expected);
            // A background run off the first row repeats the row below.
            std::vector<uint8_t> copy = {0x82};
            Put24(copy, 0x445566);
            Put24(copy, 0x778899);
            copy.push_back(0x02);
            CHECK(Rle24(copy, 2, 2) == (Pixels{0x445566, 0x778899, 0x445566, 0x778899}));
        });

        RunTest("RLE: a foreground/background image sets pixels from a bitmask, lowest bit first", []() {
            CHECK(Rle24({0x41, 0x05}, 8, 1) == (Pixels{kWhite, 0, kWhite, 0, 0, 0, 0, 0}));
            // Length from the next byte (+1), shorter than 8: only that many bits are used.
            CHECK(Rle24({0x40, 0x03, 0x09}, 4, 1) == (Pixels{kWhite, 0, 0, kWhite}));
        });

        RunTest("RLE: set-foreground orders change the colour later foreground pixels use", []() {
            std::vector<uint8_t> s = {0xC3}; // lite set-foreground run, length 3, then the new colour
            Put24(s, 0x00FF00);
            s.push_back(0x21); // a regular foreground run of 1 now uses green too
            CHECK(Rle24(s, 4, 1) == Pixels(4, 0x00FF00));
        });

        RunTest("RLE: a dithered run alternates two colours", []() {
            std::vector<uint8_t> s = {0xE2}; // lite dithered run, 2 pairs
            Put24(s, 0x111111);
            Put24(s, 0x222222);
            CHECK(Rle24(s, 4, 1) == (Pixels{0x111111, 0x222222, 0x111111, 0x222222}));
        });

        RunTest("RLE: long runs take their length from the following bytes", []() {
            std::vector<uint8_t> mega = {0xF3, 0x05, 0x00}; // mega-mega colour run, 16-bit length 5
            Put24(mega, 0xABCDEF);
            CHECK(Rle24(mega, 5, 1) == Pixels(5, 0xABCDEF));
            std::vector<uint8_t> extended = {0x60, 0x08}; // regular colour run, length 8 + 32
            Put24(extended, 0x123456);
            CHECK(Rle24(extended, 40, 1) == Pixels(40, 0x123456));
        });

        RunTest("RLE: the special orders write white, black and the two fixed bitmasks", []() {
            CHECK(Rle24({0xFD, 0xFE}, 2, 1) == (Pixels{kWhite, 0}));
            CHECK(Rle24({0xF9}, 8, 1) == (Pixels{kWhite, kWhite, 0, 0, 0, 0, 0, 0}));
            CHECK(Rle24({0xFA}, 8, 1) == (Pixels{kWhite, 0, kWhite, 0, 0, 0, 0, 0}));
        });

        RunTest("RLE: 16-bit and 15-bit pixels widen to full 8-bit channels", []() {
            Pixels out;
            std::string error;
            std::vector<uint8_t> red565 = {0x61, 0x00, 0xF8}; // colour run of 1, 0xF800
            CHECK(Rdp::DecodeInterleavedRle(red565.data(), red565.size(), 1, 1, 16, nullptr, out, error));
            CHECK(out == Pixels{0xFF0000});
            std::vector<uint8_t> white16 = {0xFD};
            CHECK(Rdp::DecodeInterleavedRle(white16.data(), white16.size(), 1, 1, 16, nullptr, out, error));
            CHECK(out == Pixels{kWhite});
            std::vector<uint8_t> green555 = {0x61, 0xE0, 0x03}; // 0x03E0
            CHECK(Rdp::DecodeInterleavedRle(green555.data(), green555.size(), 1, 1, 15, nullptr, out, error));
            CHECK(out == Pixels{0x00FF00});
        });

        RunTest("RLE: 8-bit pixels go through the palette, and need one", []() {
            std::vector<uint32_t> palette(256, 0);
            palette[7] = 0x336699;
            std::vector<uint8_t> s = {0x62, 0x07};
            Pixels out;
            std::string error;
            CHECK(Rdp::DecodeInterleavedRle(s.data(), s.size(), 2, 1, 8, palette.data(), out, error));
            CHECK(out == (Pixels{0x336699, 0x336699}));
            CHECK(!Rdp::DecodeInterleavedRle(s.data(), s.size(), 2, 1, 8, nullptr, out, error) && !error.empty());
        });

        RunTest("RLE: a stream that runs past its rectangle or stops mid-instruction fails with a reason", []() {
            Pixels out;
            std::string error;
            std::vector<uint8_t> tooLong = {0x6A, 0x01, 0x02, 0x03}; // colour run of 10 into 4 pixels
            CHECK(!Rdp::DecodeInterleavedRle(tooLong.data(), tooLong.size(), 4, 1, 24, nullptr, out, error));
            CHECK(error.find("edge") != std::string::npos);
            std::vector<uint8_t> cut = {0x64, 0x01}; // a colour run whose pixel is cut short
            CHECK(!Rdp::DecodeInterleavedRle(cut.data(), cut.size(), 4, 1, 24, nullptr, out, error));
            CHECK(error.find("pixel") != std::string::npos);
            std::vector<uint8_t> unknown = {0xFB}; // not an order
            CHECK(!Rdp::DecodeInterleavedRle(unknown.data(), unknown.size(), 4, 1, 24, nullptr, out, error));
            CHECK(!Rdp::DecodeInterleavedRle(unknown.data(), unknown.size(), 0, 1, 24, nullptr, out, error));
            CHECK(!Rdp::DecodeInterleavedRle(unknown.data(), unknown.size(), 4, 1, 32, nullptr, out, error));
        });

        RunTest("RLE: a stream that stops short leaves the rest of the rectangle black", []() {
            CHECK(Rle24({0xFD}, 3, 1) == (Pixels{kWhite, 0, 0}));
        });

        RunTest("Uncompressed bitmaps: rows bottom first, with or without padding to 4 bytes", []() {
            // 2 x 2 at 24 bits: rows of 6 bytes, padded to 8.
            std::vector<uint8_t> padded;
            Put24(padded, 0x000001);
            Put24(padded, 0x000002);
            padded.push_back(0);
            padded.push_back(0);
            Put24(padded, 0x000003);
            Put24(padded, 0x000004);
            padded.push_back(0);
            padded.push_back(0);
            Pixels out;
            std::string error;
            CHECK(Rdp::DecodeUncompressedBitmap(padded.data(), padded.size(), 2, 2, 24, nullptr, out, error));
            CHECK(out == (Pixels{3, 4, 1, 2}));
            std::vector<uint8_t> tight;
            Put24(tight, 0x000001);
            Put24(tight, 0x000002);
            Put24(tight, 0x000003);
            Put24(tight, 0x000004);
            CHECK(Rdp::DecodeUncompressedBitmap(tight.data(), tight.size(), 2, 2, 24, nullptr, out, error));
            CHECK(out == (Pixels{3, 4, 1, 2}));
            CHECK(!Rdp::DecodeUncompressedBitmap(tight.data(), tight.size() - 1, 2, 2, 24, nullptr, out, error));
            CHECK(!error.empty());
        });
    }
} // namespace MarconiTest
