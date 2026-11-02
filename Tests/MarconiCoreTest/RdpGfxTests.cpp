// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the RDP graphics pipeline pieces: RDP 8.0 bulk decompression, virtual channel chunks and dynamic channel
// PDUs, the graphics pipeline messages, the RemoteFX Progressive decoder and the surface renderer.

#include "TestSupport.h"

#include <MarconiCore/Rdp/Dvc.h>
#include <MarconiCore/Rdp/Gfx.h>
#include <MarconiCore/Rdp/GfxRenderer.h>
#include <MarconiCore/Rdp/Progressive.h>
#include <MarconiCore/Rdp/Zgfx.h>

#include <cstring>
#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        void P16(Rdp::Bytes& b, uint16_t v)
        {
            b.push_back(static_cast<uint8_t>(v));
            b.push_back(static_cast<uint8_t>(v >> 8));
        }
        void P32(Rdp::Bytes& b, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
            {
                b.push_back(static_cast<uint8_t>(v >> (8 * i)));
            }
        }
        uint32_t U32(const Rdp::Bytes& b, size_t o)
        {
            return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
                   (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
        }
        Rdp::Bytes FromString(const char* text, size_t length)
        {
            return Rdp::Bytes(reinterpret_cast<const uint8_t*>(text), reinterpret_cast<const uint8_t*>(text) + length);
        }

        // FNV-1a over the RGB bytes of each pixel, low byte first: how the vectors' expected pictures are given.
        uint32_t PictureHash(const std::vector<uint32_t>& pixels)
        {
            uint32_t hash = 2166136261u;
            for (uint32_t pixel : pixels)
            {
                pixel &= 0xFFFFFF;
                for (int i = 0; i < 4; ++i)
                {
                    hash ^= (pixel >> (8 * i)) & 0xFF;
                    hash *= 16777619u;
                }
            }
            return hash;
        }

        // A graphics pipeline PDU: header (command, flags 0, length) then `body`.
        Rdp::Bytes GfxPdu(Rdp::GfxCommand command, const Rdp::Bytes& body)
        {
            Rdp::Bytes out;
            P16(out, static_cast<uint16_t>(command));
            P16(out, 0);
            P32(out, static_cast<uint32_t>(8 + body.size()));
            out.insert(out.end(), body.begin(), body.end());
            return out;
        }

        // The ZGFX samples from MS-RDPEGFX 4.x ("The quick brown fox..."): stored, and in three segments the last
        // of which is compressed.
        const char kFox[] = "The quick brown fox jumps over the lazy dog";
        const char kFoxSingle[] = "\xE0\x04\x54\x68\x65\x20\x71\x75\x69\x63\x6B\x20\x62\x72\x6F\x77"
                                  "\x6E\x20\x66\x6F\x78\x20\x6A\x75\x6D\x70\x73\x20\x6F\x76\x65\x72"
                                  "\x20\x74\x68\x65\x20\x6C\x61\x7A\x79\x20\x64\x6F\x67";
        const char kFoxMultipart[] = "\xE1\x03\x00\x2B\x00\x00\x00\x11\x00\x00\x00\x04\x54\x68\x65\x20"
                                     "\x71\x75\x69\x63\x6B\x20\x62\x72\x6F\x77\x6E\x20\x0E\x00\x00\x00"
                                     "\x04\x66\x6F\x78\x20\x6A\x75\x6D\x70\x73\x20\x6F\x76\x65\x10\x00"
                                     "\x00\x00\x24\x39\x08\x0E\x91\xF8\xD8\x61\x3D\x1E\x44\x06\x43\x79"
                                     "\x9C\x02";

        // Progressive known answers made with FreeRDP 3.32. kEncodedTile is a 64x48 gradient with a dark square
        // from its encoder (what GNOME and KRdp send); kFirstPass and kUpgradePass are a first pass at a lower
        // quality and its refinement, reduce-extrapolate wavelet, built from arbitrary coefficient streams. The
        // expected pictures are FreeRDP's own decode of them with its plain C routines.
        const uint8_t kEncodedTile[] = {
            0xc0,0xcc,0x0c,0x00,0x00,0x00,0xca,0xac,0xcc,0xca,0x00,0x01,0xc3,0xcc,0x0a,0x00,0x00,0x00,0x00,0x40,
            0x00,0x00,0xc1,0xcc,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xc4,0xcc,0x8c,0x02,0x00,0x00,
            0x40,0x01,0x00,0x01,0x00,0x00,0x01,0x00,0x6d,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x30,0x00,
            0x66,0x66,0x77,0x88,0x98,0xc5,0xcc,0x6d,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,
            0x01,0x8a,0x00,0xbb,0x00,0x00,0x00,0x00,0x13,0xb2,0x8e,0x6d,0x98,0xe3,0xd8,0x8e,0x16,0xd4,0x70,0xb5,
            0xa3,0xa5,0xb0,0x8c,0x96,0xc2,0x32,0x5b,0x08,0xd1,0x6c,0xa3,0x65,0xa1,0x10,0x00,0xe3,0xe4,0x06,0x40,
            0xc8,0x32,0x0c,0x86,0x46,0x4d,0x9a,0x00,0x13,0x52,0x07,0x90,0x76,0x0e,0xc3,0x51,0xa9,0xa9,0xab,0x5d,
            0x90,0x00,0x00,0x1f,0x69,0x11,0x04,0xec,0x47,0x90,0xa4,0x32,0x52,0x84,0x3f,0x5f,0xfd,0x03,0x06,0x17,
            0xf8,0x5f,0x88,0x10,0x19,0x7f,0xc1,0xff,0x20,0x18,0x11,0xff,0xff,0xf0,0x1f,0x98,0x00,0x60,0x10,0xbf,
            0xfe,0x03,0xff,0x60,0x01,0x80,0x30,0x24,0x50,0x2c,0x2c,0x2c,0x78,0x58,0xf0,0xb0,0x54,0x87,0xd0,0xfa,
            0x36,0x77,0x00,0x28,0x84,0x30,0x01,0xd5,0x0a,0x29,0x0e,0xa7,0xff,0x4f,0x9f,0x5d,0x18,0x00,0x00,0x35,
            0x84,0x60,0x76,0x26,0x43,0x6a,0x4e,0x92,0x7c,0x81,0x8f,0xfe,0xc2,0xe4,0x37,0x75,0xff,0xfc,0xc0,0x03,
            0x00,0x08,0x0c,0x08,0x80,0x54,0x3a,0x15,0x16,0x55,0x11,0x7f,0xf7,0xff,0xec,0x48,0x01,0xc2,0x02,0x6f,
            0xff,0xff,0xff,0xdf,0x4a,0xe1,0x00,0x00,0xe2,0x51,0x00,0x49,0x29,0x24,0x00,0x16,0x6f,0xa0,0x08,0x7a,
            0x0b,0xe4,0x80,0x48,0x0f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xa0,
            0x02,0x20,0x09,0x80,0x38,0x24,0x0b,0x05,0x02,0xc5,0x90,0xa0,0x90,0x90,0x90,0xa1,0x62,0x92,0xe4,0x90,
            0x77,0xc8,0xf8,0x92,0x84,0x26,0x49,0x46,0xfc,0x72,0xfb,0x6a,0xe1,0x92,0xe4,0x8a,0x0c,0x93,0x8c,0x4d,
            0xf4,0xa8,0xa2,0x11,0x4d,0x24,0xdf,0x7a,0x8a,0x45,0x24,0x52,0x4d,0xfe,0xb5,0x14,0x49,0x48,0xa4,0x98,
            0x00,0x00,0x13,0xad,0x16,0xdb,0x13,0xd2,0x22,0xd1,0xf8,0xbc,0x5e,0x14,0x00,0x10,0x7d,0x00,0xd0,0x1a,
            0x06,0x83,0x41,0xa1,0xa2,0x98,0x00,0xa5,0x20,0x10,0x00,0x05,0x20,0x40,0x1e,0x81,0xe8,0xbe,0x87,0x4b,
            0xd1,0xdb,0x97,0xac,0x01,0xdc,0x81,0xa0,0xd0,0x50,0x88,0x01,0x74,0x05,0xee,0x8f,0xa1,0x3a,0x02,0x1d,
            0x04,0xec,0x63,0xc1,0x71,0xf1,0x71,0x77,0xa8,0x42,0xff,0xdf,0xeb,0x00,0x61,0x3a,0x20,0x0f,0x13,0x8b,
            0x07,0xdf,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xd1,0x04,0x90,0x59,0x05,0xa0,0x82,0x44,0x8d,0x68,0x59,
            0x27,0x92,0x74,0xaf,0x91,0x65,0xf2,0x8a,0x60,0x37,0xf2,0x4a,0x6b,0x22,0xd6,0x7f,0x92,0x56,0xf1,0xac,
            0x6b,0xfa,0x2d,0x33,0x8c,0x67,0x3f,0xd9,0x69,0x5c,0x63,0x39,0xff,0xd9,0x2b,0x4a,0x67,0x19,0x80,0x00,
            0x15,0xf3,0xfb,0xfe,0x58,0xc6,0x2c,0x21,0x8b,0x08,0x45,0x84,0x22,0xc9,0x08,0xb2,0x42,0x00,0x3c,0xf4,
            0x05,0x02,0x81,0xa0,0xd0,0xd1,0xa3,0xa7,0x00,0x09,0xd5,0x01,0x40,0x40,0x81,0x00,0x00,0x12,0x51,0x0f,
            0x21,0x24,0x29,0x16,0x8f,0xf4,0x05,0x0d,0x3f,0xd0,0x20,0xe9,0xf8,0xa1,0x59,0x78,0xa3,0xb9,0xc5,0x02,
            0xc2,0xc2,0xc7,0x85,0x8f,0x0b,0x1e,0x04,0x88,0x28,0x68,0xf4,0x04,0x90,0x01,0x0d,0x0e,0x8a,0x80,0x1d,
            0xc0,0x9d,0x7a,0x25,0x13,0xd1,0x28,0xff,0xf4,0x15,0x66,0x9a,0x7a,0x8f,0x80,0x08,0xc3,0xb0,0xac,0x2a,
            0x12,0x1c,0x5d,0xff,0xec,0x48,0x00,0x12,0x26,0x23,0x01,0x01,0x25,0x2a,0xa8,0x02,0xcb,0xe8,0x04,0x41,
            0x1f,0xfd,0x0f,0xec,0x3b,0x8e,0xa7,0x7e,0x1e,0x59,0x3f,0xff,0xca,0x81,0x03,0x8f,0x44,0x85,0x13,0xfc,
            0x54,0x08,0x94,0x46,0xf6,0x14,0x0f,0xf1,0x50,0x1e,0x88,0x1a,0xe4,0xe8,0x9f,0xe2,0xa0,0x3c,0x99,0xe7,
            0xa0,0x44,0xff,0x15,0x02,0x08,0x10,0x20,0x40,0x89,0xfe,0x1a,0x04,0x08,0x10,0x40,0x81,0x1b,0xf8,0x68,
            0x10,0x20,0x41,0x02,0x04,0x60,0xc2,0xcc,0x06,0x00,0x00,0x00,
        };
        const uint8_t kFirstPass[] = {
            0xc0,0xcc,0x0c,0x00,0x00,0x00,0xca,0xac,0xcc,0xca,0x00,0x01,0xc3,0xcc,0x0a,0x00,0x00,0x00,0x00,0x40,
            0x00,0x01,0xc1,0xcc,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xc4,0xcc,0xb6,0x00,0x00,0x00,
            0x40,0x01,0x00,0x01,0x02,0x01,0x01,0x00,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x40,0x00,
            0x87,0x87,0x87,0x87,0x87,0x00,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
            0x22,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc6,0xcc,0x77,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x18,0x00,0x18,0x00,0x00,0x00,
            0x49,0x00,0x08,0xe0,0x00,0x02,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x04,0x00,0x04,0x01,0x44,0x00,0x00,
            0x22,0x04,0x00,0x00,0x01,0x00,0xa2,0x00,0x01,0x00,0x20,0x03,0x0e,0x00,0x40,0x01,0x04,0x40,0x02,0x00,
            0x20,0x24,0x00,0x10,0x40,0x00,0x40,0x00,0x00,0x40,0x86,0x04,0x00,0x00,0x02,0x00,0x01,0x00,0x00,0x04,
            0x00,0x08,0x00,0x10,0x00,0x40,0x00,0x00,0x10,0x21,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x10,0x00,0x00,0x88,0x00,0x04,0x14,0x00,0x00,0x00,0x10,0x80,0x00,0x01,0xc2,0xcc,0x06,0x00,
            0x00,0x00,
        };
        const uint8_t kUpgradePass[] = {
            0xc1,0xcc,0x0c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0xc4,0xcc,0xb6,0x00,0x00,0x00,0x40,0x01,
            0x00,0x01,0x02,0x01,0x01,0x00,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x40,0x00,0x87,0x87,
            0x87,0x87,0x87,0x00,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x32,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc7,0xcc,0x77,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x0c,0x00,0x0a,0x00,0x0e,0x00,0x10,0x00,0x12,0x00,0x17,
            0x00,0x00,0x41,0x00,0x00,0x00,0x04,0x00,0x00,0x04,0x00,0x00,0x00,0x58,0x07,0x83,0x15,0xb3,0xe3,0xea,
            0xd4,0x15,0x97,0x00,0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x08,0x20,0x28,0x90,0x0c,0x5a,0xf5,0xea,0x5d,
            0x89,0x87,0x85,0xbf,0x97,0x27,0xbf,0x8b,0xd3,0xf7,0x10,0xa2,0xb5,0x50,0x00,0x20,0x00,0x44,0x00,0x20,
            0x00,0x90,0x00,0x00,0x00,0x04,0x81,0x00,0x10,0x40,0x00,0x7c,0x2e,0x32,0xe9,0xda,0xb4,0x7d,0x71,0xd2,
            0xb5,0xe4,0xfe,0xd3,0xdb,0x3b,0x17,0x21,0xa2,0x38,0xce,0x06,0x9a,0xea,0xc2,0xcc,0x06,0x00,0x00,0x00,
        };
    } // namespace

    void RunRdpGfxTests()
    {
        RunTest("RDP 8.0 bulk data: the specification's stored and compressed samples", []() {
            Rdp::ZgfxDecompressor zgfx;
            Rdp::Bytes out;
            std::string error;
            Rdp::Bytes single = FromString(kFoxSingle, sizeof(kFoxSingle) - 1);
            CHECK(zgfx.Decompress(single.data(), single.size(), out, error));
            CHECK(std::string(out.begin(), out.end()) == kFox);
            Rdp::ZgfxDecompressor fresh;
            Rdp::Bytes multipart = FromString(kFoxMultipart, sizeof(kFoxMultipart) - 1);
            CHECK(fresh.Decompress(multipart.data(), multipart.size(), out, error));
            CHECK(std::string(out.begin(), out.end()) == kFox);
            // Something that isn't segmented data, and a truncated message, are refused.
            Rdp::Bytes junk = {0x12, 0x34};
            CHECK(!fresh.Decompress(junk.data(), junk.size(), out, error));
            Rdp::Bytes cut(multipart.begin(), multipart.end() - 5);
            Rdp::ZgfxDecompressor other;
            CHECK(!other.Decompress(cut.data(), cut.size(), out, error));
        });

        RunTest("RDP 8.0 bulk data: a match reaches back into an earlier message", []() {
            // Message 1 stores "abcdef"; message 2 is compressed: a match of distance 6, length 6, then literal 'g'.
            Rdp::ZgfxDecompressor zgfx;
            Rdp::Bytes first = {0xE0, 0x04, 'a', 'b', 'c', 'd', 'e', 'f'};
            Rdp::Bytes out;
            std::string error;
            CHECK(zgfx.Decompress(first.data(), first.size(), out, error));
            // Bits: 10001 (a distance token with 5 value bits) 00110 (6), 1 0 10 (count 4 + 2 = 6), then 0 01100111
            // ('g'): 23 bits, 10001001 10101000 1100111x, so the last byte says 1 bit of padding.
            Rdp::Bytes second = {0xE0, 0x24, 0x89, 0xA8, 0xCE, 0x01};
            CHECK(zgfx.Decompress(second.data(), second.size(), out, error));
            CHECK(std::string(out.begin(), out.end()) == "abcdefg");
        });

        RunTest("Static channel messages are chunked and put back together", []() {
            Rdp::Bytes message(4000);
            for (size_t i = 0; i < message.size(); ++i)
            {
                message[i] = static_cast<uint8_t>(i * 7);
            }
            std::vector<Rdp::Bytes> chunks = Rdp::BuildChannelChunks(message);
            CHECK(chunks.size() == 3);
            CHECK(U32(chunks[0], 0) == 4000 && U32(chunks[0], 4) == 0x01 && chunks[0].size() == 8 + 1600);
            CHECK(U32(chunks[1], 4) == 0x00 && U32(chunks[2], 4) == 0x02 && chunks[2].size() == 8 + 800);
            Rdp::ChannelReassembler reassembler;
            Rdp::Bytes whole;
            std::string error;
            CHECK(!reassembler.Add(chunks[0], whole, error) && error.empty());
            CHECK(!reassembler.Add(chunks[1], whole, error) && error.empty());
            CHECK(reassembler.Add(chunks[2], whole, error) && whole == message);
            // A middle chunk with no first one is an error; a single-chunk message stands alone.
            CHECK(!reassembler.Add(chunks[1], whole, error) && !error.empty());
            std::vector<Rdp::Bytes> small = Rdp::BuildChannelChunks({1, 2, 3});
            CHECK(small.size() == 1 && U32(small[0], 4) == 0x03);
            CHECK(reassembler.Add(small[0], whole, error) && whole == Rdp::Bytes({1, 2, 3}));
        });

        RunTest("Dynamic channel PDUs parse and build", []() {
            Rdp::DvcPdu pdu;
            std::string error;
            // The server's capability request (version 3, with priority charges).
            CHECK(Rdp::ParseDvcPdu({0x50, 0x00, 0x03, 0x00, 1, 0, 2, 0, 3, 0, 4, 0}, pdu, error));
            CHECK(pdu.command == Rdp::DvcCommand::Capability && pdu.version == 3);
            CHECK(Rdp::BuildDvcCapabilityResponse(2) == Rdp::Bytes({0x50, 0x00, 0x02, 0x00}));
            // A create request with a 2-byte channel id.
            Rdp::Bytes create = {0x11, 0x34, 0x12};
            std::string name = Rdp::kGfxChannelName;
            create.insert(create.end(), name.begin(), name.end());
            create.push_back(0);
            CHECK(Rdp::ParseDvcPdu(create, pdu, error));
            CHECK(pdu.command == Rdp::DvcCommand::Create && pdu.channelId == 0x1234 && pdu.channelName == name);
            CHECK(Rdp::BuildDvcCreateResponse(0x1234, 0) == Rdp::Bytes({0x11, 0x34, 0x12, 0, 0, 0, 0}));
            CHECK(Rdp::BuildDvcClose(5) == Rdp::Bytes({0x40, 0x05}));
            // Data split into DataFirst (total length in a 2-byte field) and Data, put back together.
            Rdp::DvcReassembler reassembler;
            Rdp::Bytes message;
            CHECK(Rdp::ParseDvcPdu({0x24, 0x07, 0x05, 0x00, 'a', 'b', 'c'}, pdu, error));
            CHECK(pdu.command == Rdp::DvcCommand::DataFirst && pdu.totalLength == 5);
            CHECK(!reassembler.Add(pdu, message, error) && error.empty());
            CHECK(Rdp::ParseDvcPdu({0x30, 0x07, 'd', 'e'}, pdu, error));
            CHECK(reassembler.Add(pdu, message, error) && message == Rdp::Bytes({'a', 'b', 'c', 'd', 'e'}));
            // What the client sends: one Data PDU when it fits, DataFirst and Data otherwise.
            std::vector<Rdp::Bytes> pdus = Rdp::BuildDvcData(7, Rdp::Bytes(10, 0xAB));
            CHECK(pdus.size() == 1 && pdus[0].size() == 12 && pdus[0][0] == 0x30 && pdus[0][1] == 7);
            pdus = Rdp::BuildDvcData(7, Rdp::Bytes(3000, 0xAB));
            CHECK(pdus.size() == 2 && pdus[0][0] == 0x24 && pdus[0].size() == 1600 && pdus[1][0] == 0x30);
            Rdp::DvcReassembler back;
            size_t total = 0;
            for (const Rdp::Bytes& piece : pdus)
            {
                CHECK(Rdp::ParseDvcPdu(piece, pdu, error));
                if (back.Add(pdu, message, error))
                {
                    total = message.size();
                }
            }
            CHECK(total == 3000);
        });

        RunTest("Graphics pipeline messages parse, and the client's are laid out as the specification says", []() {
            Rdp::Bytes caps = Rdp::BuildGfxCapsAdvertise();
            CHECK(caps.size() == 8 + 2 + 4 * 12 && U32(caps, 4) == caps.size() && caps[0] == 0x12);
            CHECK(U32(caps, 10) == Rdp::kGfxCapVersion107 && U32(caps, 14) == 4);
            CHECK(U32(caps, 18) == (Rdp::kGfxCapsAvcDisabled | Rdp::kGfxCapsScaledMapDisable));
            Rdp::Bytes ack = Rdp::BuildGfxFrameAcknowledge(9, 3);
            CHECK(ack.size() == 20 && ack[0] == 0x0D && U32(ack, 12) == 9 && U32(ack, 16) == 3);

            Rdp::Bytes message;
            Rdp::Bytes body;
            P16(body, 3); P16(body, 640); P16(body, 480); body.push_back(0x20);
            Rdp::Bytes pdu = GfxPdu(Rdp::GfxCommand::CreateSurface, body);
            message.insert(message.end(), pdu.begin(), pdu.end());
            body.clear();
            P16(body, 3);
            body.insert(body.end(), {0x10, 0x20, 0x30, 0xFF}); // B, G, R, XA
            P16(body, 1); P16(body, 1); P16(body, 2); P16(body, 5); P16(body, 6);
            pdu = GfxPdu(Rdp::GfxCommand::SolidFill, body);
            message.insert(message.end(), pdu.begin(), pdu.end());
            body.clear();
            P16(body, 3); P16(body, 9); P32(body, 1); body.push_back(0x20); P32(body, 3); body.insert(body.end(), {7, 8, 9});
            pdu = GfxPdu(Rdp::GfxCommand::WireToSurface2, body);
            message.insert(message.end(), pdu.begin(), pdu.end());
            std::vector<Rdp::GfxPdu> pdus;
            std::string error;
            CHECK(Rdp::ParseGfxPdus(message, pdus, error) && pdus.size() == 3);
            CHECK(pdus[0].surfaceId == 3 && pdus[0].width == 640 && pdus[0].height == 480);
            CHECK(pdus[1].fillColor == 0x302010 && pdus[1].rects.size() == 1 && pdus[1].rects[0].right == 5);
            CHECK(pdus[2].codecId == 9 && pdus[2].codecContextId == 1 && pdus[2].data == Rdp::Bytes({7, 8, 9}));
            // A PDU whose contents run past its length is refused.
            message[message.size() - 7] = 200; // WireToSurface2's bitmapDataLength (then 3 bytes of data)
            CHECK(!Rdp::ParseGfxPdus(message, pdus, error));
        });

        RunTest("RemoteFX Progressive: a picture from FreeRDP's encoder decodes as FreeRDP decodes it", []() {
            Rdp::ProgressiveSurface surface;
            std::vector<uint32_t> pixels(64 * 48, 0);
            std::vector<Rdp::ProgressiveRect> updated;
            std::string error;
            CHECK(Rdp::DecodeProgressive(kEncodedTile, sizeof(kEncodedTile), surface, pixels.data(), 64, 48, 64,
                                         updated, error));
            CHECK(PictureHash(pixels) == 0xc9441110);
            CHECK(pixels[5 * 64 + 5] == 0x141880 && pixels[20 * 64 + 30] == 0x1d1d1d);
            CHECK(updated.size() == 1 && updated[0].right == 64 && updated[0].bottom == 48);
        });

        RunTest("RemoteFX Progressive: a first pass and a refinement pass, as Windows sends", []() {
            Rdp::ProgressiveSurface surface;
            std::vector<uint32_t> pixels(64 * 64, 0);
            std::vector<Rdp::ProgressiveRect> updated;
            std::string error;
            CHECK(Rdp::DecodeProgressive(kFirstPass, sizeof(kFirstPass), surface, pixels.data(), 64, 64, 64, updated,
                                         error));
            CHECK(PictureHash(pixels) == 0xb12160be && surface.subbandDiffing);
            CHECK(Rdp::DecodeProgressive(kUpgradePass, sizeof(kUpgradePass), surface, pixels.data(), 64, 64, 64,
                                         updated, error));
            CHECK(PictureHash(pixels) == 0xde1cc010 && pixels[10 * 64 + 10] == 0x847f86);
            // A truncated message is refused.
            Rdp::Bytes broken(kUpgradePass, kUpgradePass + sizeof(kUpgradePass));
            broken.resize(broken.size() - 10);
            Rdp::ProgressiveSurface other;
            CHECK(!Rdp::DecodeProgressive(broken.data(), broken.size(), other, pixels.data(), 64, 64, 64, updated,
                                          error) && !error.empty());
        });

        RunTest("The graphics renderer draws surfaces into the framebuffer and acknowledges frames", []() {
            Rfb::Framebuffer framebuffer;
            int resizes = 0;
            Rdp::GfxRenderer renderer(framebuffer, [&](int width, int height) {
                framebuffer.Resize(width, height);
                ++resizes;
            });
            auto pdu = [](Rdp::GfxCommand command) {
                Rdp::GfxPdu p;
                p.command = command;
                return p;
            };
            std::vector<Rdp::GfxPdu> pdus;
            Rdp::GfxPdu reset = pdu(Rdp::GfxCommand::ResetGraphics);
            reset.width = 40;
            reset.height = 30;
            pdus.push_back(reset);
            Rdp::GfxPdu create = pdu(Rdp::GfxCommand::CreateSurface);
            create.surfaceId = 1;
            create.width = 20;
            create.height = 20;
            pdus.push_back(create);
            Rdp::GfxPdu map = pdu(Rdp::GfxCommand::MapSurfaceToOutput);
            map.surfaceId = 1;
            map.outputX = 10;
            map.outputY = 5;
            pdus.push_back(map);
            Rdp::GfxPdu fill = pdu(Rdp::GfxCommand::SolidFill);
            fill.surfaceId = 1;
            fill.fillColor = 0xFF0000;
            fill.rects.push_back({0, 0, 4, 4});
            pdus.push_back(fill);
            // An uncompressed 2x1 picture (B, G, R, X) at 6,0.
            Rdp::GfxPdu raw = pdu(Rdp::GfxCommand::WireToSurface1);
            raw.surfaceId = 1;
            raw.rect = {6, 0, 8, 1};
            raw.data = {0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00};
            pdus.push_back(raw);
            // Overlapping copy within the surface: the red square moved right by 2.
            Rdp::GfxPdu copy = pdu(Rdp::GfxCommand::SurfaceToSurface);
            copy.sourceSurfaceId = 1;
            copy.surfaceId = 1;
            copy.rect = {0, 0, 4, 4};
            copy.points.push_back({2, 0});
            pdus.push_back(copy);
            // Into the cache and back out lower down.
            Rdp::GfxPdu toCache = pdu(Rdp::GfxCommand::SurfaceToCache);
            toCache.surfaceId = 1;
            toCache.cacheSlot = 3;
            toCache.rect = {0, 0, 2, 2};
            pdus.push_back(toCache);
            Rdp::GfxPdu fromCache = pdu(Rdp::GfxCommand::CacheToSurface);
            fromCache.surfaceId = 1;
            fromCache.cacheSlot = 3;
            fromCache.points.push_back({0, 10});
            pdus.push_back(fromCache);
            Rdp::GfxPdu end = pdu(Rdp::GfxCommand::EndFrame);
            end.frameId = 42;
            pdus.push_back(end);
            Rdp::GfxRenderer::Result result;
            std::string error;
            CHECK(renderer.Handle(pdus, result, error));
            CHECK(resizes == 1 && framebuffer.GetWidth() == 40 && framebuffer.GetHeight() == 30);
            CHECK(framebuffer.GetPixel(10, 5) == 0xFF0000 && framebuffer.GetPixel(15, 8) == 0xFF0000);
            CHECK(framebuffer.GetPixel(15, 5) == 0xFF0000);  // surface 5,0: the square, moved right by 2
            CHECK(framebuffer.GetPixel(16, 5) == 0x00FF00);  // surface 6,0 and 7,0: the uncompressed pixels
            CHECK(framebuffer.GetPixel(17, 5) == 0x0000FF);
            CHECK(framebuffer.GetPixel(16, 9) == 0);         // surface 6,4: below the square
            CHECK(framebuffer.GetPixel(10, 15) == 0xFF0000 && framebuffer.GetPixel(12, 15) == 0);
            CHECK(framebuffer.GetPixel(9, 5) == 0);           // outside the surface
            CHECK(!result.changed.empty() && result.replies.size() == 1 && renderer.FramesDecoded() == 1);
            CHECK(U32(result.replies[0], 12) == 42);
            // A picture in a codec Marconi doesn't decode is skipped, not fatal.
            Rdp::GfxPdu avc = pdu(Rdp::GfxCommand::WireToSurface1);
            avc.surfaceId = 1;
            avc.codecId = static_cast<uint16_t>(Rdp::GfxCodec::Avc420);
            avc.rect = {0, 0, 16, 16};
            avc.data = {1, 2, 3};
            Rdp::GfxRenderer::Result again;
            CHECK(renderer.Handle({avc}, again, error));
            // A surface of an impossible size is.
            Rdp::GfxPdu huge = create;
            huge.width = 100000;
            CHECK(!renderer.Handle({huge}, again, error) && !error.empty());
        });
    }
} // namespace MarconiTest
