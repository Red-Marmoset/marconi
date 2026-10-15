// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the Client message loop against a fake server: updates, resize, bell, clipboard, input, disconnects

#include "TestSupport.h"

#include <MarconiCore/Rfb/Messages.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        struct ConnectedSession
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};

            ConnectedSession(int width, int height)
            {
                Rfb::ConnectionOptions options;
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
                server.HandshakeNone("RFB 003.008\n", width, height, "session");
                server.ReadClientSetup();
                if (!listener.WaitFor([&]() { return listener.connected; }))
                {
                    throw std::runtime_error("client never finished connecting");
                }
            }

            // Reads the incremental FramebufferUpdateRequest the client sends after each update.
            void ExpectUpdateRequest(bool incremental, int width, int height)
            {
                CHECK(server.In().U8() == Rfb::ClientMessage::FramebufferUpdateRequest);
                CHECK(server.In().U8() == (incremental ? 1 : 0));
                CHECK(server.In().U16() == 0 && server.In().U16() == 0);
                CHECK(server.In().U16() == width);
                CHECK(server.In().U16() == height);
            }
        };

        void RectHeader(Rfb::OutBuffer& out, int x, int y, int w, int h, int32_t encoding)
        {
            out.U16(static_cast<uint16_t>(x)).U16(static_cast<uint16_t>(y));
            out.U16(static_cast<uint16_t>(w)).U16(static_cast<uint16_t>(h)).S32(encoding);
        }
    } // namespace

    void RunClientTests()
    {
        RunTest("Raw framebuffer update is applied", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 1, 1, 2, 2, Rfb::Encoding::Raw);
            for (uint32_t pixel : {0x00112233u, 0x00445566u, 0x00778899u, 0xffaabbccu})
            {
                update.Append(&pixel, 4); // little-endian on the test machine, as the client requested
            }
            s.server.Send(update);
            s.ExpectUpdateRequest(true, 4, 3);
            CHECK(s.listener.WaitFor([&]() { return s.listener.updates == 1; }));
            CHECK(s.listener.lastRects.size() == 1 && s.listener.lastRects[0].width == 2);

            Rfb::Framebuffer& fb = s.client.GetFramebuffer();
            std::lock_guard<std::mutex> lock(fb.GetMutex());
            CHECK(fb.GetPixel(1, 1) == 0x00112233u);
            CHECK(fb.GetPixel(2, 1) == 0x00445566u);
            CHECK(fb.GetPixel(1, 2) == 0x00778899u);
            CHECK(fb.GetPixel(2, 2) == 0x00aabbccu); // padding byte is masked off
            CHECK(fb.GetPixel(0, 0) == 0);
        });

        RunTest("DesktopSize resizes and requests a full update", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 0, 0, 10, 7, Rfb::Encoding::DesktopSize);
            s.server.Send(update);
            s.ExpectUpdateRequest(false, 10, 7);
            s.ExpectUpdateRequest(true, 10, 7);
            CHECK(s.listener.WaitFor([&]() { return s.listener.resizedWidth == 10; }));
            CHECK(s.listener.resizedHeight == 7);
            CHECK(s.client.GetFramebuffer().GetWidth() == 10);
        });

        RunTest("ExtendedDesktopSize resizes; a refused client request does not", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(2);
            RectHeader(update, 1, 3, 99, 99, Rfb::Encoding::ExtendedDesktopSize); // our request, refused
            update.U8(1).Padding(3).U32(0).U16(0).U16(0).U16(99).U16(99).U32(0);
            RectHeader(update, 0, 0, 12, 9, Rfb::Encoding::ExtendedDesktopSize); // server-side change
            update.U8(2).Padding(3);
            update.U32(1).U16(0).U16(0).U16(6).U16(9).U32(0);
            update.U32(2).U16(6).U16(0).U16(6).U16(9).U32(0);
            s.server.Send(update);
            s.ExpectUpdateRequest(false, 12, 9);
            s.ExpectUpdateRequest(true, 12, 9);
            CHECK(s.listener.resizedWidth == 12 && s.listener.resizedHeight == 9);
        });

        RunTest("Empty cursor shape hides the cursor", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 0, 0, 0, 0, Rfb::Encoding::Cursor);
            s.server.Send(update);
            s.ExpectUpdateRequest(true, 4, 3);
            CHECK(s.listener.WaitFor([&]() { return s.listener.cursorUpdates == 1; }));
            CHECK(s.listener.cursor.width == 0 && s.listener.cursor.argb.empty());
        });

        RunTest("Cursor mask decoding", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 1, 2, 9, 2, Rfb::Encoding::Cursor);
            for (int i = 0; i < 18; ++i)
            {
                uint32_t pixel = i == 0 ? 0x00ff0000u : 0x00ffffffu;
                update.Append(&pixel, 4);
            }
            update.U8(0x80).U8(0x80).U8(0x00).U8(0x00);
            s.server.Send(update);
            s.ExpectUpdateRequest(true, 4, 3);
            CHECK(s.listener.WaitFor([&]() { return s.listener.cursorUpdates == 1; }));
            const Rfb::CursorShape& c = s.listener.cursor;
            CHECK(c.width == 9 && c.height == 2 && c.hotspotX == 1 && c.hotspotY == 2);
            CHECK(c.argb.size() == 18);
            CHECK(c.argb[0] == 0xffff0000u);  // visible red
            CHECK(c.argb[1] == 0x00ffffffu);  // transparent
            CHECK(c.argb[8] == 0xffffffffu);  // visible white (second mask byte)
            CHECK(c.argb[9] == 0x00ffffffu);  // row 1 hidden
        });

        RunTest("Implausible desktop size is refused", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 0, 0, 60000, 60000, Rfb::Encoding::DesktopSize);
            s.server.Send(update);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("implausibly large") != std::string::npos);
        });

        RunTest("Refresh after the connection dropped doesn't throw", []() {
            ConnectedSession s(4, 3);
            s.server.Close();
            for (int i = 0; i < 20; ++i)
            {
                s.client.RequestFullUpdate(); // must never throw, connected or not
            }
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            s.client.RequestFullUpdate();
        });

        RunTest("Bell, colour map and server cut text", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer messages;
            messages.U8(Rfb::ServerMessage::Bell);
            messages.U8(Rfb::ServerMessage::SetColourMapEntries).Padding(1).U16(0).U16(1).Padding(6);
            messages.U8(Rfb::ServerMessage::ServerCutText).Padding(3).U32(5).Append("hello");
            s.server.Send(messages);
            CHECK(s.listener.WaitFor([&]() { return s.listener.bells == 1 && s.listener.cutTexts.size() == 1; }));
            CHECK(s.listener.cutTexts[0] == "hello");
        });

        RunTest("Input messages reach the server", []() {
            ConnectedSession s(4, 3);
            s.client.SendPointerEvent(300, 200, 0x05);
            s.client.SendKeyEvent(0xff0d, true);
            s.client.SendClientCutText("clip");

            Rfb::InStream& in = s.server.In();
            CHECK(in.U8() == Rfb::ClientMessage::PointerEvent);
            CHECK(in.U8() == 0x05);
            CHECK(in.U16() == 300 && in.U16() == 200);
            CHECK(in.U8() == Rfb::ClientMessage::KeyEvent);
            CHECK(in.U8() == 1);
            in.Skip(2);
            CHECK(in.U32() == 0xff0du);
            CHECK(in.U8() == Rfb::ClientMessage::ClientCutText);
            in.Skip(3);
            CHECK(in.U32() == 4);
            CHECK(in.ReadString(4) == "clip");
        });

        RunTest("Out-of-bounds rectangle disconnects with an error", []() {
            ConnectedSession s(4, 3);
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 3, 0, 2, 1, Rfb::Encoding::Raw);
            s.server.Send(update);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("outside") != std::string::npos);
        });

        RunTest("Server closing the connection is reported", []() {
            ConnectedSession s(4, 3);
            s.server.Close();
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(!s.listener.disconnectError.empty());
            CHECK(!s.client.IsConnected());
        });

        RunTest("Local disconnect reports no error", []() {
            ConnectedSession s(4, 3);
            s.client.Disconnect();
            CHECK(s.listener.disconnected);
            CHECK(s.listener.disconnectError.empty());
        });

        RunTest("Connection refused is reported", []() {
            uint16_t port;
            {
                FakeServer unused; // grab a free port, then release it
                port = unused.GetPort();
            }
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = port;
            options.connectTimeoutMs = 3000;
            client.Connect(options);
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            CHECK(!listener.disconnectError.empty());
            printf("  (error was: %s)\n", listener.disconnectError.c_str());
        });

        RunTest("Disconnect while connecting returns promptly", []() {
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "10.255.255.1"; // non-routable: connect hangs until the timeout
            options.port = 5900;
            options.connectTimeoutMs = 20000;
            client.Connect(options);
            auto start = std::chrono::steady_clock::now();
            client.Disconnect();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            CHECK(elapsed.count() < 2000);
            CHECK(listener.disconnected && listener.disconnectError.empty());
        });
    }
} // namespace MarconiTest
