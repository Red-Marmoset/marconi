// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol's messages: what the server and client send each other, and how each turns into bytes
//
// Design notes (the reasons are in SERVER_PLAN.md, "The Marconi protocol"):
//  - Push, not request-reply: the server sends a picture whenever the screen changed, so no frame costs a round
//    trip the way VNC's do.
//  - Lean: little-endian, varint lengths, one type byte per message, no padding, no per-message capability bytes.
//    Capabilities are agreed once, in Hello/Welcome.
//  - Every message is self-delimiting on the wire through the frame envelope below, so a reader that meets a type
//    it doesn't know skips the whole message by its length instead of losing the stream.

#pragma once

#include <MarconiCore/Wire/WireStream.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        // Sent first, before any TLS, so a probe (and a human with a packet trace) can tell a Marconi server from
        // anything else on the port. Four bytes, then nothing until the TLS handshake starts.
        constexpr uint8_t kMagic[4] = {'M', 'R', 'C', 'N'};

        // The protocol version both ends speak. Bumped when the wire format changes in a way older builds can't
        // read; Hello/Welcome carry it so each end knows what the other understands.
        constexpr uint16_t kProtocolVersion = 1;

        // The port a Marconi server listens on (TCP, and UDP from Phase 5). See SERVER_PLAN.md for why 1895.
        constexpr uint16_t kDefaultPort = 1895;

        // What each end can do, agreed once in Hello (what the client offers) and Welcome (what the server will
        // use). A bit the other end doesn't set is a feature neither uses on this connection. Bits are never
        // reused; a new feature takes the next free bit, so old and new builds still understand each other.
        namespace Capability
        {
            constexpr uint64_t Video = 1u << 0;            // H.264 (and later) video frames; Phase 4
            constexpr uint64_t Datagram = 1u << 1;         // the UDP channel for video and input; Phase 5
            constexpr uint64_t Clipboard = 1u << 2;        // clipboard text and images
            constexpr uint64_t Files = 1u << 3;            // file transfer
            constexpr uint64_t Gamepads = 1u << 4;         // forwarded game controllers; Phase 7
            constexpr uint64_t MultipleDisplays = 1u << 5; // choose one of several displays
            constexpr uint64_t ResizeDisplay = 1u << 6;    // ask the server to match its screen to the window
        } // namespace Capability

        // The type byte at the start of every message's payload. Control-channel and datagram messages share one
        // numbering so a type is never ambiguous. Gaps are left between groups for messages added later.
        enum class Type : uint8_t
        {
            // Setup and connection (control channel)
            Hello = 1,       // client -> server: version, capabilities offered
            Welcome = 2,     // server -> client: version, capabilities granted, name, displays
            Login = 3,       // client -> server: the access password
            LoginResult = 4, // server -> client: ok, wrong, or locked out
            Ping = 5,        // either way: round-trip timing / keep-alive
            Pong = 6,        // the answer to a Ping, echoing its token
            Bye = 7,         // either way: closing cleanly, with a reason

            // Display and picture (control channel in Phase 1; video moves to datagrams in Phase 5)
            Displays = 16,       // server -> client: the layout of its displays
            SelectDisplay = 17,  // client -> server: stream just this display (0 = all)
            SetDisplayMode = 18, // client -> server: resize this display to match the window
            StreamConfig = 19,   // client -> server: codec, bitrate, frame rate, mode
            Still = 20,          // server -> client: one lossless rectangle (zlib)
            Move = 21,           // server -> client: a rectangle copied from elsewhere on the screen
            FrameBegin = 22,     // server -> client: a picture update starts (frame id, display)
            FrameEnd = 23,       // server -> client: the update is complete
            CursorShape = 24,    // server -> client: the pointer image, or hidden

            // Input (control channel for keys; pointer moves to datagrams in Phase 5)
            Key = 32,     // client -> server: a key by scancode and keysym, down or up
            Text = 33,    // client -> server: a string to type (characters with no obvious key)
            Pointer = 34, // client -> server: absolute position, buttons, wheel

            // Clipboard (control channel)
            ClipboardOffer = 48,   // either way: what formats the clipboard now holds
            ClipboardRequest = 49, // either way: send me this format
            ClipboardData = 50,    // either way: the clipboard contents
        };

        // One rectangle of the screen. Plain ints so the protocol layer needs nothing from the VNC code.
        struct Rect
        {
            int32_t x = 0;
            int32_t y = 0;
            int32_t width = 0;
            int32_t height = 0;
        };

        // One of the server's displays and where it sits in the whole desktop.
        struct Display
        {
            uint32_t id = 0;
            int32_t x = 0;
            int32_t y = 0;
            int32_t width = 0;
            int32_t height = 0;
        };

        // -- The frame envelope: how a whole message sits on the control channel --------------------------------
        //
        // varint totalLength, then that many bytes: one Type byte and the message body. The length lets a reader
        // take in a whole message before parsing it, and skip a type it doesn't know. kMaxMessageBytes (in
        // WireStream.h) caps it so a hostile or corrupt length can't make the reader allocate wildly; a picture
        // larger than this is split across several Still messages.

        // Wraps an already-built message body (its first byte is the Type) in the length prefix.
        Bytes Frame(const Bytes& body);
        // Builds a message: writes the type byte, calls `writeBody(w)` to add the rest, returns the framed bytes.
        template <class WriteBody>
        Bytes Build(Type type, WriteBody writeBody)
        {
            Writer body;
            body.U8(static_cast<uint8_t>(type));
            writeBody(body);
            return Frame(body.Data());
        }

        // -- The messages --------------------------------------------------------------------------------------
        //
        // Each has Encode (append the framed message to a Writer, or return the framed bytes) and a static Decode
        // that reads the body from a Reader positioned just after the type byte. Decode throws WireError on
        // anything malformed.

        struct Hello
        {
            uint16_t version = kProtocolVersion;
            uint64_t capabilities = 0;
            std::string clientName; // for the server's log and its "someone connected" notice

            Bytes Encode() const;
            static Hello Decode(Reader& reader);
        };

        struct Welcome
        {
            uint16_t version = kProtocolVersion;
            uint64_t capabilities = 0; // the subset of Hello's the server will use
            std::string serverName;    // the computer's name, for the title bar
            std::string serverVersion; // Marconi Server's version string
            std::vector<Display> displays;

            Bytes Encode() const;
            static Welcome Decode(Reader& reader);
        };

        struct Login
        {
            std::string password;

            Bytes Encode() const;
            static Login Decode(Reader& reader);
        };

        struct LoginResult
        {
            enum class Status : uint8_t
            {
                Ok = 0,
                WrongPassword = 1,
                LockedOut = 2, // too many wrong tries; `retryAfterSeconds` says when to try again
                NoPassword = 3 // the server has no password set, so it can't let anyone in
            };
            Status status = Status::Ok;
            uint32_t retryAfterSeconds = 0;

            Bytes Encode() const;
            static LoginResult Decode(Reader& reader);
        };

        struct Ping
        {
            uint64_t token = 0;

            Bytes Encode() const;
            static Ping Decode(Reader& reader);
        };

        struct Pong
        {
            uint64_t token = 0;

            Bytes Encode() const;
            static Pong Decode(Reader& reader);
        };

        struct Bye
        {
            std::string reason; // empty for a clean, expected close

            Bytes Encode() const;
            static Bye Decode(Reader& reader);
        };

        struct Displays
        {
            std::vector<Display> displays;

            Bytes Encode() const;
            static Displays Decode(Reader& reader);
        };

        struct SelectDisplay
        {
            uint32_t displayId = 0; // 0 = the whole desktop

            Bytes Encode() const;
            static SelectDisplay Decode(Reader& reader);
        };

        struct SetDisplayMode
        {
            uint32_t displayId = 0;
            int32_t width = 0;
            int32_t height = 0;

            Bytes Encode() const;
            static SetDisplayMode Decode(Reader& reader);
        };

        struct StreamConfig
        {
            enum class Mode : uint8_t
            {
                StillOnly = 0, // lossless rectangles only (Phase 1, and the fallback)
                Video = 1,     // video for moving areas (Phase 4)
                Hybrid = 2     // video while it moves, lossless when it stops (Phase 4)
            };
            Mode mode = Mode::StillOnly;
            uint32_t maxBitrateKbps = 0; // 0 = no limit
            uint16_t maxFps = 0;         // 0 = the display's own rate

            Bytes Encode() const;
            static StreamConfig Decode(Reader& reader);
        };

        struct FrameBegin
        {
            uint64_t frameId = 0;
            uint32_t displayId = 0;

            Bytes Encode() const;
            static FrameBegin Decode(Reader& reader);
        };

        struct FrameEnd
        {
            uint64_t frameId = 0;

            Bytes Encode() const;
            static FrameEnd Decode(Reader& reader);
        };

        struct Still
        {
            enum class Compression : uint8_t
            {
                None = 0, // raw 0x00RRGGBB pixels, little-endian, row by row
                Zlib = 1  // the same pixels, zlib-deflated
            };
            Rect rect;
            Compression compression = Compression::Zlib;
            Bytes data; // width*height*4 bytes once decompressed

            Bytes Encode() const;
            static Still Decode(Reader& reader);
        };

        struct Move
        {
            Rect destination;
            int32_t sourceX = 0;
            int32_t sourceY = 0;

            Bytes Encode() const;
            static Move Decode(Reader& reader);
        };

        struct CursorShape
        {
            int32_t width = 0;
            int32_t height = 0;
            int32_t hotspotX = 0;
            int32_t hotspotY = 0;
            Bytes argb; // width*height ARGB pixels (alpha 0 or 255); empty = the pointer is hidden

            Bytes Encode() const;
            static CursorShape Decode(Reader& reader);
        };

        struct Key
        {
            uint32_t keysym = 0;   // an X11 keysym, as the VNC side uses (what the key means)
            uint32_t scancode = 0; // the physical key (USB HID usage), 0 if unknown; lets games see WASD as WASD
            bool down = false;

            Bytes Encode() const;
            static Key Decode(Reader& reader);
        };

        struct Text
        {
            std::string utf8; // characters to type that have no plain key

            Bytes Encode() const;
            static Text Decode(Reader& reader);
        };

        struct Pointer
        {
            int32_t x = 0; // whole-desktop coordinates
            int32_t y = 0;
            uint8_t buttons = 0;     // bit 0 left, 1 middle, 2 right, 3/4 wheel up/down (as VNC)
            int16_t wheelHigh = 0;   // high-resolution vertical wheel, 120 per notch; 0 if none
            int16_t wheelHighX = 0;  // high-resolution horizontal wheel

            Bytes Encode() const;
            static Pointer Decode(Reader& reader);
        };

        struct ClipboardOffer
        {
            bool hasText = false;
            bool hasImage = false;

            Bytes Encode() const;
            static ClipboardOffer Decode(Reader& reader);
        };

        struct ClipboardRequest
        {
            enum class Format : uint8_t
            {
                Text = 0,
                Image = 1
            };
            Format format = Format::Text;

            Bytes Encode() const;
            static ClipboardRequest Decode(Reader& reader);
        };

        struct ClipboardData
        {
            ClipboardRequest::Format format = ClipboardRequest::Format::Text;
            Bytes data; // UTF-8 for text, PNG for an image

            Bytes Encode() const;
            static ClipboardData Decode(Reader& reader);
        };

        // Reads the type byte at the reader's current position without consuming the rest, for dispatching to the
        // right Decode. The reader is left just after the type byte, where every Decode expects to start.
        Type ReadType(Reader& reader);
    } // namespace Wire
} // namespace Marconi
