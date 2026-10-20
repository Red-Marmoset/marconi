// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP input events (keysym to scancode, the input PDU) and output parsing (bitmap updates, slow and fast
// path). MS-RDPBCGR 2.2.8.1.1.3 and 2.2.9.1.

#include "TestSupport.h"

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/Rdp/Input.h>
#include <MarconiCore/Rdp/Output.h>
#include <MarconiCore/Rdp/Pdu.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        uint16_t U16(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o]) | (static_cast<uint16_t>(b[o + 1]) << 8); }
    } // namespace

    void RunRdpInputOutputTests()
    {
        RunTest("Keysyms map to the expected set-1 scancodes", []() {
            CHECK(Rdp::KeysymToScancode('a').code == 0x1E && !Rdp::KeysymToScancode('a').extended);
            CHECK(Rdp::KeysymToScancode('A').code == 0x1E); // case folds; Shift is separate
            CHECK(Rdp::KeysymToScancode('z').code == 0x2C);
            CHECK(Rdp::KeysymToScancode('1').code == 0x02);
            CHECK(Rdp::KeysymToScancode('!').code == 0x02); // shifted digit shares the scancode
            CHECK(Rdp::KeysymToScancode(' ').code == 0x39);
            CHECK(Rdp::KeysymToScancode(Keysym::Return).code == 0x1C);
            CHECK(Rdp::KeysymToScancode(Keysym::Escape).code == 0x01);
            CHECK(Rdp::KeysymToScancode(Keysym::F1).code == 0x3B);
            CHECK(Rdp::KeysymToScancode(Keysym::F1 + 11).code == 0x58); // F12
            // Arrows and right-hand modifiers are extended keys.
            CHECK(Rdp::KeysymToScancode(Keysym::Left).extended && Rdp::KeysymToScancode(Keysym::Left).code == 0x4B);
            CHECK(Rdp::KeysymToScancode(Keysym::Control_R).extended && Rdp::KeysymToScancode(Keysym::Control_R).code == 0x1D);
            CHECK(!Rdp::KeysymToScancode(0x1234).known); // an unmapped keysym
        });

        RunTest("A scancode event carries the code and the down/up and extended flags", []() {
            Rdp::Bytes down = Rdp::ScancodeEvent(Rdp::KeysymToScancode('a'), true);
            CHECK(down.size() == 12);
            CHECK(U16(down, 4) == 0x0004);  // messageType: scancode
            CHECK(U16(down, 6) == 0x0000);  // flags: down, not extended
            CHECK(U16(down, 8) == 0x1E);    // keyCode
            Rdp::Bytes up = Rdp::ScancodeEvent(Rdp::KeysymToScancode('a'), false);
            CHECK(U16(up, 6) == 0x8000);    // release
            Rdp::Bytes rightCtrl = Rdp::ScancodeEvent(Rdp::KeysymToScancode(Keysym::Control_R), true);
            CHECK(U16(rightCtrl, 6) == 0x0100); // extended
        });

        RunTest("A mouse event carries the flags and position", []() {
            Rdp::Bytes move = Rdp::MouseEvent(Rdp::kPtrMove, 100, 200);
            CHECK(U16(move, 4) == 0x8001);        // messageType: mouse
            CHECK(U16(move, 6) == Rdp::kPtrMove);
            CHECK(U16(move, 8) == 100 && U16(move, 10) == 200);
            Rdp::Bytes leftDown = Rdp::MouseEvent(Rdp::kPtrDown | Rdp::kPtrButton1, 5, 6);
            CHECK(U16(leftDown, 6) == (0x8000 | 0x1000));
        });

        RunTest("The input PDU wraps events in a Share Data PDU of type Input", []() {
            std::vector<Rdp::Bytes> events = {Rdp::MouseEvent(Rdp::kPtrMove, 1, 2),
                                              Rdp::ScancodeEvent(Rdp::KeysymToScancode('x'), true)};
            Rdp::Bytes pdu = Rdp::BuildInputPdu(0x000A0001, 1007, events);
            Rdp::ShareDataType type2;
            Rdp::Bytes body;
            CHECK(Rdp::ReadShareData(pdu, type2, body));
            CHECK(type2 == Rdp::ShareDataType::Input);
            CHECK(U16(body, 0) == 2); // numberEvents
            CHECK(body.size() == 4 + events[0].size() + events[1].size());
        });

        RunTest("A slow-path bitmap update parses into rectangles", []() {
            // One uncompressed rectangle, 2x1 at (3,4), 16bpp, 4 bytes of data.
            Rdp::Bytes body;
            auto p16 = [&](uint16_t v) { body.push_back(static_cast<uint8_t>(v)); body.push_back(static_cast<uint8_t>(v >> 8)); };
            p16(1);          // number of rectangles
            p16(3);          // destLeft
            p16(4);          // destTop
            p16(4);          // destRight (inclusive)
            p16(4);          // destBottom
            p16(2);          // width
            p16(1);          // height
            p16(16);         // bitsPerPixel
            p16(0);          // flags: uncompressed
            p16(4);          // bitmapLength
            body.insert(body.end(), {0x11, 0x22, 0x33, 0x44});
            std::vector<Rdp::BitmapRect> rects;
            std::string error;
            CHECK(Rdp::ParseBitmapUpdate(body, rects, error));
            CHECK(rects.size() == 1);
            CHECK(rects[0].x == 3 && rects[0].y == 4 && rects[0].width == 2 && rects[0].height == 1);
            CHECK(rects[0].bitsPerPixel == 16 && !rects[0].compressed);
            CHECK(rects[0].data.size() == 4);
        });

        RunTest("A compressed rectangle skips its 8-byte compression header", []() {
            Rdp::Bytes body;
            auto p16 = [&](uint16_t v) { body.push_back(static_cast<uint8_t>(v)); body.push_back(static_cast<uint8_t>(v >> 8)); };
            p16(1);
            p16(0); p16(0); p16(63); p16(63); // a 64x64 area
            p16(64); p16(64); p16(16);
            p16(0x0001);     // flags: compressed, with the header present
            p16(8 + 3);      // bitmapLength: header + 3 body bytes
            // Compressed data header: cbCompFirstRowSize(2)=0, cbCompMainBodySize(2)=3, cbScanWidth(2), cbUncompressedSize(2)
            p16(0); p16(3); p16(128); p16(8192);
            body.insert(body.end(), {0xAA, 0xBB, 0xCC});
            std::vector<Rdp::BitmapRect> rects;
            std::string error;
            CHECK(Rdp::ParseBitmapUpdate(body, rects, error));
            CHECK(rects.size() == 1 && rects[0].compressed);
            CHECK(rects[0].data == Rdp::Bytes({0xAA, 0xBB, 0xCC})); // just the body, header stripped
        });

        RunTest("A fast-path bitmap update is recognised and parsed", []() {
            // Build the inner TS_UPDATE_BITMAP: updateType 0x0001, then one small uncompressed rectangle.
            Rdp::Bytes inner;
            auto p16 = [&](uint16_t v) { inner.push_back(static_cast<uint8_t>(v)); inner.push_back(static_cast<uint8_t>(v >> 8)); };
            p16(0x0001);     // updateType: BITMAP
            p16(1);          // count
            p16(0); p16(0); p16(0); p16(0);
            p16(1); p16(1); p16(16); p16(0); p16(2);
            inner.insert(inner.end(), {0x01, 0x02});
            // Fast-path update header: updateCode 0x01 (BITMAP), then a 2-byte size.
            Rdp::Bytes payload = {0x01};
            payload.push_back(static_cast<uint8_t>(inner.size()));
            payload.push_back(static_cast<uint8_t>(inner.size() >> 8));
            payload.insert(payload.end(), inner.begin(), inner.end());
            std::vector<Rdp::FastPathUpdate> updates;
            std::string error;
            CHECK(Rdp::ParseFastPathUpdates(payload, updates, error));
            CHECK(updates.size() == 1);
            CHECK(updates[0].type == Rdp::FastPathUpdateType::Bitmap);
            CHECK(updates[0].rects.size() == 1 && updates[0].rects[0].data.size() == 2);

            // A pointer update code is recognised as such, not treated as bitmap.
            Rdp::Bytes pointer = {0x05, 0x00, 0x00};
            updates.clear();
            CHECK(Rdp::ParseFastPathUpdates(pointer, updates, error));
            CHECK(updates.size() == 1 && updates[0].type == Rdp::FastPathUpdateType::Pointer);
        });

        RunTest("A truncated bitmap update fails with a reason, not a crash", []() {
            std::vector<Rdp::BitmapRect> rects;
            std::string error;
            Rdp::Bytes shortBody = {0x01, 0x00, 0x00}; // claims a rectangle, no data
            CHECK(!Rdp::ParseBitmapUpdate(shortBody, rects, error) && !error.empty());
            std::vector<Rdp::FastPathUpdate> updates;
            Rdp::Bytes shortFast = {0x01, 0x10, 0x00}; // claims 16 bytes, none follow
            CHECK(!Rdp::ParseFastPathUpdates(shortFast, updates, error) && !error.empty());
        });
    }
} // namespace MarconiTest
