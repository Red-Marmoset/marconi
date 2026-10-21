// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the Marconi protocol's messages into bytes and back

#include <MarconiCore/Wire/WireMessages.h>

#include <chrono>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            void WriteDisplay(Writer& w, const Display& display)
            {
                w.U32(display.id).S32(display.x).S32(display.y).S32(display.width).S32(display.height);
            }

            Display ReadDisplay(Reader& r)
            {
                Display display;
                display.id = r.U32();
                display.x = r.S32();
                display.y = r.S32();
                display.width = r.S32();
                display.height = r.S32();
                return display;
            }

            void WriteRect(Writer& w, const Rect& rect)
            {
                w.S32(rect.x).S32(rect.y).S32(rect.width).S32(rect.height);
            }

            Rect ReadRect(Reader& r)
            {
                Rect rect;
                rect.x = r.S32();
                rect.y = r.S32();
                rect.width = r.S32();
                rect.height = r.S32();
                return rect;
            }

            // A varint count of displays, then each one.
            void WriteDisplayList(Writer& w, const std::vector<Display>& displays)
            {
                w.Varint(displays.size());
                for (const Display& display : displays)
                {
                    WriteDisplay(w, display);
                }
            }

            std::vector<Display> ReadDisplayList(Reader& r)
            {
                uint64_t count = r.Varint();
                // Each display is 20 bytes, so a count larger than what's left can't be real: refuse before
                // reserving, so a bad count can't ask for a huge vector.
                if (count > r.Remaining() / 20 + 1)
                {
                    throw WireError("more displays than the message can hold");
                }
                std::vector<Display> displays;
                displays.reserve(static_cast<size_t>(count));
                for (uint64_t i = 0; i < count; ++i)
                {
                    displays.push_back(ReadDisplay(r));
                }
                return displays;
            }
        } // namespace

        uint64_t NowMicros()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count());
        }

        Bytes Frame(const Bytes& body)
        {
            Writer framed;
            framed.Varint(body.size());
            framed.Raw(body);
            return framed.Take();
        }

        Type ReadType(Reader& reader)
        {
            return static_cast<Type>(reader.U8());
        }

        Bytes Hello::Encode() const
        {
            return Build(Type::Hello, [&](Writer& w) { w.U16(version).U64(capabilities).String(clientName); });
        }

        Hello Hello::Decode(Reader& r)
        {
            Hello message;
            message.version = r.U16();
            message.capabilities = r.U64();
            message.clientName = r.String();
            return message;
        }

        Bytes Welcome::Encode() const
        {
            return Build(Type::Welcome, [&](Writer& w) {
                w.U16(version).U64(capabilities).String(serverName).String(serverVersion);
                WriteDisplayList(w, displays);
            });
        }

        Welcome Welcome::Decode(Reader& r)
        {
            Welcome message;
            message.version = r.U16();
            message.capabilities = r.U64();
            message.serverName = r.String();
            message.serverVersion = r.String();
            message.displays = ReadDisplayList(r);
            return message;
        }

        Bytes Login::Encode() const
        {
            return Build(Type::Login, [&](Writer& w) { w.String(password); });
        }

        Login Login::Decode(Reader& r)
        {
            Login message;
            message.password = r.String();
            return message;
        }

        Bytes LoginResult::Encode() const
        {
            return Build(Type::LoginResult,
                         [&](Writer& w) { w.U8(static_cast<uint8_t>(status)).Varint(retryAfterSeconds); });
        }

        LoginResult LoginResult::Decode(Reader& r)
        {
            LoginResult message;
            message.status = static_cast<Status>(r.U8());
            message.retryAfterSeconds = static_cast<uint32_t>(r.Varint());
            return message;
        }

        Bytes Ping::Encode() const
        {
            return Build(Type::Ping, [&](Writer& w) { w.U64(token); });
        }

        Ping Ping::Decode(Reader& r)
        {
            Ping message;
            message.token = r.U64();
            return message;
        }

        Bytes Pong::Encode() const
        {
            return Build(Type::Pong, [&](Writer& w) {
                w.U64(token);
                if (hasTimes)
                {
                    w.Varint(receivedMicros).Varint(answeredMicros);
                }
            });
        }

        Pong Pong::Decode(Reader& r)
        {
            Pong message;
            message.token = r.U64();
            // The times were added later behind the Timestamps capability; an older server's Pong ends here.
            if (!r.AtEnd())
            {
                message.hasTimes = true;
                message.receivedMicros = r.Varint();
                message.answeredMicros = r.Varint();
            }
            return message;
        }

        Bytes Bye::Encode() const
        {
            return Build(Type::Bye, [&](Writer& w) { w.String(reason); });
        }

        Bye Bye::Decode(Reader& r)
        {
            Bye message;
            message.reason = r.String();
            return message;
        }

        Bytes Displays::Encode() const
        {
            return Build(Type::Displays, [&](Writer& w) { WriteDisplayList(w, displays); });
        }

        Displays Displays::Decode(Reader& r)
        {
            Displays message;
            message.displays = ReadDisplayList(r);
            return message;
        }

        Bytes SelectDisplay::Encode() const
        {
            return Build(Type::SelectDisplay, [&](Writer& w) { w.U32(displayId); });
        }

        SelectDisplay SelectDisplay::Decode(Reader& r)
        {
            SelectDisplay message;
            message.displayId = r.U32();
            return message;
        }

        Bytes SetDisplayMode::Encode() const
        {
            return Build(Type::SetDisplayMode, [&](Writer& w) { w.U32(displayId).S32(width).S32(height); });
        }

        SetDisplayMode SetDisplayMode::Decode(Reader& r)
        {
            SetDisplayMode message;
            message.displayId = r.U32();
            message.width = r.S32();
            message.height = r.S32();
            return message;
        }

        Bytes StreamConfig::Encode() const
        {
            return Build(Type::StreamConfig, [&](Writer& w) {
                w.U8(static_cast<uint8_t>(mode)).Varint(maxBitrateKbps).U16(maxFps);
            });
        }

        StreamConfig StreamConfig::Decode(Reader& r)
        {
            StreamConfig message;
            message.mode = static_cast<Mode>(r.U8());
            message.maxBitrateKbps = static_cast<uint32_t>(r.Varint());
            message.maxFps = r.U16();
            return message;
        }

        Bytes FrameBegin::Encode() const
        {
            return Build(Type::FrameBegin, [&](Writer& w) { w.U64(frameId).U32(displayId); });
        }

        FrameBegin FrameBegin::Decode(Reader& r)
        {
            FrameBegin message;
            message.frameId = r.U64();
            message.displayId = r.U32();
            return message;
        }

        Bytes FrameEnd::Encode() const
        {
            return Build(Type::FrameEnd, [&](Writer& w) {
                w.U64(frameId);
                if (hasTimes)
                {
                    w.Varint(captureMicros).Varint(sentMicros).Varint(compressMicros);
                }
            });
        }

        FrameEnd FrameEnd::Decode(Reader& r)
        {
            FrameEnd message;
            message.frameId = r.U64();
            // The times were added later behind the Timestamps capability; an older server's FrameEnd ends here.
            if (!r.AtEnd())
            {
                message.hasTimes = true;
                message.captureMicros = r.Varint();
                message.sentMicros = r.Varint();
                message.compressMicros = static_cast<uint32_t>(r.Varint());
            }
            return message;
        }

        Bytes Still::Encode() const
        {
            return Build(Type::Still, [&](Writer& w) {
                WriteRect(w, rect);
                w.U8(static_cast<uint8_t>(compression)).Blob(data);
            });
        }

        Still Still::Decode(Reader& r)
        {
            Still message;
            message.rect = ReadRect(r);
            message.compression = static_cast<Compression>(r.U8());
            message.data = r.Blob();
            return message;
        }

        Bytes Move::Encode() const
        {
            return Build(Type::Move, [&](Writer& w) {
                WriteRect(w, destination);
                w.S32(sourceX).S32(sourceY);
            });
        }

        Move Move::Decode(Reader& r)
        {
            Move message;
            message.destination = ReadRect(r);
            message.sourceX = r.S32();
            message.sourceY = r.S32();
            return message;
        }

        Bytes CursorShape::Encode() const
        {
            return Build(Type::CursorShape, [&](Writer& w) {
                w.S32(width).S32(height).S32(hotspotX).S32(hotspotY).Blob(argb);
            });
        }

        CursorShape CursorShape::Decode(Reader& r)
        {
            CursorShape message;
            message.width = r.S32();
            message.height = r.S32();
            message.hotspotX = r.S32();
            message.hotspotY = r.S32();
            message.argb = r.Blob();
            return message;
        }

        Bytes Key::Encode() const
        {
            return Build(Type::Key, [&](Writer& w) { w.U32(keysym).U32(scancode).U8(down ? 1 : 0); });
        }

        Key Key::Decode(Reader& r)
        {
            Key message;
            message.keysym = r.U32();
            message.scancode = r.U32();
            message.down = r.U8() != 0;
            return message;
        }

        Bytes Text::Encode() const
        {
            return Build(Type::Text, [&](Writer& w) { w.String(utf8); });
        }

        Text Text::Decode(Reader& r)
        {
            Text message;
            message.utf8 = r.String();
            return message;
        }

        Bytes Pointer::Encode() const
        {
            return Build(Type::Pointer, [&](Writer& w) {
                w.S32(x).S32(y).U8(buttons).U16(static_cast<uint16_t>(wheelHigh)).U16(static_cast<uint16_t>(wheelHighX));
            });
        }

        Pointer Pointer::Decode(Reader& r)
        {
            Pointer message;
            message.x = r.S32();
            message.y = r.S32();
            message.buttons = r.U8();
            message.wheelHigh = static_cast<int16_t>(r.U16());
            message.wheelHighX = static_cast<int16_t>(r.U16());
            return message;
        }

        Bytes ClipboardOffer::Encode() const
        {
            return Build(Type::ClipboardOffer,
                         [&](Writer& w) { w.U8((hasText ? 1 : 0) | (hasImage ? 2 : 0)); });
        }

        ClipboardOffer ClipboardOffer::Decode(Reader& r)
        {
            ClipboardOffer message;
            uint8_t flags = r.U8();
            message.hasText = (flags & 1) != 0;
            message.hasImage = (flags & 2) != 0;
            return message;
        }

        Bytes ClipboardRequest::Encode() const
        {
            return Build(Type::ClipboardRequest, [&](Writer& w) { w.U8(static_cast<uint8_t>(format)); });
        }

        ClipboardRequest ClipboardRequest::Decode(Reader& r)
        {
            ClipboardRequest message;
            message.format = static_cast<Format>(r.U8());
            return message;
        }

        Bytes ClipboardData::Encode() const
        {
            return Build(Type::ClipboardData, [&](Writer& w) { w.U8(static_cast<uint8_t>(format)).Blob(data); });
        }

        ClipboardData ClipboardData::Decode(Reader& r)
        {
            ClipboardData message;
            message.format = static_cast<ClipboardRequest::Format>(r.U8());
            message.data = r.Blob();
            return message;
        }
    } // namespace Wire
} // namespace Marconi
