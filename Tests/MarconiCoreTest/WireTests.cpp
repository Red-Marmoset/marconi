// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the Marconi protocol: the little-endian stream, varints, and every message's round trip; plus a fuzz
// pass that feeds random bytes to the decoders and checks they refuse rubbish cleanly instead of crashing

#include "TestSupport.h"

#include <MarconiCore/Wire/WireCompress.h>
#include <MarconiCore/Wire/WireMessages.h>
#include <MarconiCore/Wire/WireStream.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // Encodes a message, checks the frame envelope (one length prefix, matching the bytes, the right type),
        // decodes it and checks the body was fully consumed, then returns the decoded copy. The framed bytes stay
        // in this function's scope while Decode runs, so the returned value (its own strings and vectors) is safe
        // to use afterwards; a Wire::Reader must never outlive the bytes it reads.
        template <class Message>
        Message RoundTrip(const Message& message, Wire::Type expected)
        {
            Wire::Bytes framed = message.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            CHECK(length == outer.Remaining());
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == expected);
            Message back = Message::Decode(body);
            CHECK(body.AtEnd());
            return back;
        }

        // xorshift32 with a fixed seed, as RandomInputTests uses: the same bytes every run, so a failure replays.
        class Random
        {
        public:
            explicit Random(uint32_t seed) : m_state(seed) {}
            uint32_t Next()
            {
                m_state ^= m_state << 13;
                m_state ^= m_state >> 17;
                m_state ^= m_state << 5;
                return m_state;
            }
            Wire::Bytes Bytes(size_t count)
            {
                Wire::Bytes bytes(count);
                for (uint8_t& byte : bytes)
                {
                    byte = static_cast<uint8_t>(Next() >> 24);
                }
                return bytes;
            }

        private:
            uint32_t m_state;
        };
    } // namespace

    void RunWireTests()
    {
        RunTest("Writer and Reader round-trip fixed-width values", []() {
            Wire::Writer w;
            w.U8(0x12).U16(0x3456).U32(0x789abcdeu).U64(0x0102030405060708ull).S32(-5);
            Wire::Reader r(w.Data());
            CHECK(r.U8() == 0x12);
            CHECK(r.U16() == 0x3456);
            CHECK(r.U32() == 0x789abcdeu);
            CHECK(r.U64() == 0x0102030405060708ull);
            CHECK(r.S32() == -5);
            CHECK(r.AtEnd());
        });

        RunTest("values are little-endian on the wire", []() {
            Wire::Writer w;
            w.U16(0x0102).U32(0x01020304u);
            CHECK(w.Data() == HexBytes("0201 04030201"));
        });

        RunTest("varints round-trip and use the documented lengths", []() {
            const uint64_t values[] = {0, 1, 127, 128, 300, 16383, 16384, 0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
            for (uint64_t value : values)
            {
                Wire::Writer w;
                w.Varint(value);
                CHECK(w.Size() == Wire::VarintSize(value));
                Wire::Reader r(w.Data());
                CHECK(r.Varint() == value);
                CHECK(r.AtEnd());
            }
            // Small numbers are one byte; 128 is the first that needs two.
            CHECK(Wire::VarintSize(127) == 1);
            CHECK(Wire::VarintSize(128) == 2);
            CHECK(Wire::VarintSize(0xFFFFFFFFFFFFFFFFull) == 10);
        });

        RunTest("blobs and strings carry a length", []() {
            Wire::Writer w;
            w.String("hello").Blob(HexBytes("deadbeef"));
            Wire::Bytes bytes = w.Take();
            Wire::Reader r(bytes);
            CHECK(r.String() == "hello");
            CHECK(r.Blob() == HexBytes("deadbeef"));
            CHECK(r.AtEnd());
        });

        RunTest("reading past the end throws, not crashes", []() {
            // A Reader is a view: the bytes it reads must outlive it, so each is a named local here.
            Wire::Bytes shortValue = HexBytes("01 02");
            Wire::Reader r(shortValue);
            CHECK(ThrowsWireError([&]() { r.U32(); }));
            Wire::Bytes shortBlob = HexBytes("05 41 42"); // a blob claiming 5 bytes but holding 2
            Wire::Reader r2(shortBlob);
            CHECK(ThrowsWireError([&]() { r2.Blob(); }));
            Wire::Bytes endlessVarint = HexBytes("ff ff ff ff ff ff ff ff ff ff ff"); // a varint that never ends
            Wire::Reader r3(endlessVarint);
            CHECK(ThrowsWireError([&]() { r3.Varint(); }));
        });

        RunTest("the frame envelope carries one whole message", []() {
            Wire::Hello hello;
            hello.version = 1;
            hello.capabilities = Wire::Capability::Clipboard | Wire::Capability::Files;
            hello.clientName = "Sem's laptop";
            Wire::Hello back = RoundTrip(hello, Wire::Type::Hello);
            CHECK(back.version == hello.version);
            CHECK(back.capabilities == hello.capabilities);
            CHECK(back.clientName == hello.clientName);
        });

        RunTest("Welcome round-trips its display list", []() {
            Wire::Welcome welcome;
            welcome.capabilities = Wire::Capability::Video;
            welcome.serverName = "Studio PC";
            welcome.serverVersion = "0.6.0.123";
            welcome.displays = {{1, 0, 0, 2560, 1440}, {2, 2560, 0, 1920, 1080}};
            Wire::Welcome back = RoundTrip(welcome, Wire::Type::Welcome);
            CHECK(back.serverName == "Studio PC");
            CHECK(back.serverVersion == "0.6.0.123");
            CHECK(back.displays.size() == 2);
            CHECK(back.displays[1].id == 2 && back.displays[1].x == 2560 && back.displays[1].width == 1920);
        });

        RunTest("login and its result round-trip", []() {
            Wire::Login login{"hunter2"};
            CHECK(RoundTrip(login, Wire::Type::Login).password == "hunter2");

            Wire::LoginResult result;
            result.status = Wire::LoginResult::Status::LockedOut;
            result.retryAfterSeconds = 30;
            Wire::LoginResult back = RoundTrip(result, Wire::Type::LoginResult);
            CHECK(back.status == Wire::LoginResult::Status::LockedOut);
            CHECK(back.retryAfterSeconds == 30);
        });

        RunTest("a Still rectangle round-trips its pixels", []() {
            Wire::Still still;
            still.rect = {10, 20, 3, 2};
            still.compression = Wire::Still::Compression::None;
            still.data = HexBytes("00112233 44556677 8899aabb ccddeeff 01234567 89abcdef");
            Wire::Still back = RoundTrip(still, Wire::Type::Still);
            CHECK(back.rect.x == 10 && back.rect.y == 20 && back.rect.width == 3 && back.rect.height == 2);
            CHECK(back.compression == Wire::Still::Compression::None);
            CHECK(back.data == still.data);
        });

        RunTest("input messages round-trip", []() {
            Wire::Key key;
            key.keysym = 0x0061; // 'a'
            key.scancode = 0x04; // USB HID 'a'
            key.down = true;
            Wire::Key kback = RoundTrip(key, Wire::Type::Key);
            CHECK(kback.keysym == 0x0061 && kback.scancode == 0x04 && kback.down);

            Wire::Pointer pointer;
            pointer.x = 1920;
            pointer.y = -5; // a negative coordinate must survive (signed)
            pointer.buttons = 0x05;
            pointer.wheelHigh = -120;
            Wire::Pointer pback = RoundTrip(pointer, Wire::Type::Pointer);
            CHECK(pback.x == 1920 && pback.y == -5 && pback.buttons == 0x05 && pback.wheelHigh == -120);
        });

        RunTest("clipboard messages round-trip", []() {
            Wire::ClipboardOffer offer{true, false};
            Wire::ClipboardOffer oback = RoundTrip(offer, Wire::Type::ClipboardOffer);
            CHECK(oback.hasText && !oback.hasImage);

            Wire::ClipboardData data;
            data.format = Wire::ClipboardRequest::Format::Text;
            data.data = HexBytes("48 65 6c 6c 6f"); // "Hello"
            Wire::ClipboardData dback = RoundTrip(data, Wire::Type::ClipboardData);
            CHECK(dback.format == Wire::ClipboardRequest::Format::Text);
            CHECK(dback.data == data.data);
        });

        RunTest("still pictures compress and come back the same", []() {
            // A repetitive run (like a flat screen) shrinks a lot; the round trip is exact.
            Wire::Bytes original(8192);
            for (size_t i = 0; i < original.size(); ++i)
            {
                original[i] = static_cast<uint8_t>((i / 64) & 0xFF);
            }
            Wire::Bytes squeezed = Wire::ZlibCompress(original);
            CHECK(!squeezed.empty() && squeezed.size() < original.size());
            Wire::Bytes back;
            CHECK(Wire::ZlibDecompress(squeezed, back, original.size()));
            CHECK(back == original);
            // The wrong expected size is refused rather than trusted.
            Wire::Bytes wrong;
            CHECK(!Wire::ZlibDecompress(squeezed, wrong, original.size() + 1));
            // Rubbish in is refused, not crashed on.
            CHECK(!Wire::ZlibDecompress(HexBytes("deadbeef"), wrong, 64));
        });

        RunTest("Pong and FrameEnd carry the server's times only when asked to", []() {
            Wire::Pong plain;
            plain.token = 42;
            Wire::Pong plainBack = RoundTrip(plain, Wire::Type::Pong);
            CHECK(plainBack.token == 42);
            CHECK(!plainBack.hasTimes);

            Wire::Pong timed;
            timed.token = 7;
            timed.hasTimes = true;
            timed.receivedMicros = 1234567890123ull;
            timed.answeredMicros = 1234567890456ull;
            Wire::Pong timedBack = RoundTrip(timed, Wire::Type::Pong);
            CHECK(timedBack.token == 7);
            CHECK(timedBack.hasTimes);
            CHECK(timedBack.receivedMicros == 1234567890123ull);
            CHECK(timedBack.answeredMicros == 1234567890456ull);

            Wire::FrameEnd end;
            end.frameId = 99;
            Wire::FrameEnd endBack = RoundTrip(end, Wire::Type::FrameEnd);
            CHECK(endBack.frameId == 99);
            CHECK(!endBack.hasTimes);

            end.hasTimes = true;
            end.captureMicros = 5000000;
            end.sentMicros = 5004321;
            end.compressMicros = 1200;
            Wire::FrameEnd timedEnd = RoundTrip(end, Wire::Type::FrameEnd);
            CHECK(timedEnd.hasTimes);
            CHECK(timedEnd.captureMicros == 5000000);
            CHECK(timedEnd.sentMicros == 5004321);
            CHECK(timedEnd.compressMicros == 1200);
        });

        RunTest("an older reader skips the times it doesn't know", []() {
            // What a build from before the Timestamps capability reads: the fields it knows, then it stops, and the
            // message's length (not its contents) moves the stream on. So a timed Pong costs an old client nothing.
            Wire::Pong timed;
            timed.token = 3;
            timed.hasTimes = true;
            timed.receivedMicros = 10;
            timed.answeredMicros = 20;
            Wire::Bytes framed = timed.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == Wire::Type::Pong);
            CHECK(body.U64() == 3); // all an old Pong::Decode reads
            CHECK(!body.AtEnd());   // the rest is skipped by length
        });

        RunTest("capability bits from 32 up are real 64-bit values", []() {
            CHECK(Wire::Capability::Timestamps == 128u);
            constexpr uint64_t high = uint64_t{1} << 40;
            Wire::Hello hello;
            hello.capabilities = high | Wire::Capability::Timestamps;
            CHECK(RoundTrip(hello, Wire::Type::Hello).capabilities == (high | Wire::Capability::Timestamps));
        });

        RunTest("random bytes are decoded or refused, never crash", []() {
            // Every decoder, driven straight off random bodies: whatever the bytes, Decode either returns or throws
            // WireError. Anything else (a segfault, a std::bad_alloc from a huge length, another exception type)
            // fails the test.
            Random random(0xC0FFEEu);
            auto tryDecode = [](Wire::Type type, const Wire::Bytes& body) {
                Wire::Reader reader(body);
                try
                {
                    switch (type)
                    {
                    case Wire::Type::Hello: Wire::Hello::Decode(reader); break;
                    case Wire::Type::Welcome: Wire::Welcome::Decode(reader); break;
                    case Wire::Type::Login: Wire::Login::Decode(reader); break;
                    case Wire::Type::LoginResult: Wire::LoginResult::Decode(reader); break;
                    case Wire::Type::Ping: Wire::Ping::Decode(reader); break;
                    case Wire::Type::Pong: Wire::Pong::Decode(reader); break;
                    case Wire::Type::Bye: Wire::Bye::Decode(reader); break;
                    case Wire::Type::Displays: Wire::Displays::Decode(reader); break;
                    case Wire::Type::SelectDisplay: Wire::SelectDisplay::Decode(reader); break;
                    case Wire::Type::SetDisplayMode: Wire::SetDisplayMode::Decode(reader); break;
                    case Wire::Type::StreamConfig: Wire::StreamConfig::Decode(reader); break;
                    case Wire::Type::FrameBegin: Wire::FrameBegin::Decode(reader); break;
                    case Wire::Type::FrameEnd: Wire::FrameEnd::Decode(reader); break;
                    case Wire::Type::Still: Wire::Still::Decode(reader); break;
                    case Wire::Type::Move: Wire::Move::Decode(reader); break;
                    case Wire::Type::CursorShape: Wire::CursorShape::Decode(reader); break;
                    case Wire::Type::Key: Wire::Key::Decode(reader); break;
                    case Wire::Type::Text: Wire::Text::Decode(reader); break;
                    case Wire::Type::Pointer: Wire::Pointer::Decode(reader); break;
                    case Wire::Type::ClipboardOffer: Wire::ClipboardOffer::Decode(reader); break;
                    case Wire::Type::ClipboardRequest: Wire::ClipboardRequest::Decode(reader); break;
                    case Wire::Type::ClipboardData: Wire::ClipboardData::Decode(reader); break;
                    }
                }
                catch (const Wire::WireError&)
                {
                    // The intended way to reject bad input.
                }
            };
            const Wire::Type types[] = {
                Wire::Type::Hello,       Wire::Type::Welcome,       Wire::Type::Login,
                Wire::Type::LoginResult, Wire::Type::Bye,           Wire::Type::Displays,
                Wire::Type::StreamConfig, Wire::Type::Still,        Wire::Type::Move,
                Wire::Type::CursorShape, Wire::Type::Key,           Wire::Type::Text,
                Wire::Type::Pointer,     Wire::Type::ClipboardData, Wire::Type::Pong,
                Wire::Type::FrameEnd};
            for (Wire::Type type : types)
            {
                for (int i = 0; i < 2000; ++i)
                {
                    tryDecode(type, random.Bytes(static_cast<size_t>(random.Next() % 64)));
                }
            }
            CHECK(true); // reaching here without crashing is the result
        });
    }
} // namespace MarconiTest
