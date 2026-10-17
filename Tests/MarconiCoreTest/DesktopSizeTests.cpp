// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the remote display list (ExtendedDesktopSize) and asking the server to change its screen size

#include "TestSupport.h"

#include <MarconiCore/Rfb/Messages.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        struct Session
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};

            Session()
            {
                Rfb::ConnectionOptions options;
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
                server.HandshakeNone("RFB 003.008\n", 200, 100, "session");
                server.ReadClientSetup();
                if (!listener.WaitFor([&]() { return listener.connected; }))
                {
                    throw std::runtime_error("client never finished connecting");
                }
            }

            // An ExtendedDesktopSize rectangle: `reason` and `status` ride in x and y.
            void SendLayout(int reason, int status, int width, int height, const std::vector<Rfb::Screen>& screens)
            {
                Rfb::OutBuffer update;
                update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
                update.U16(static_cast<uint16_t>(reason)).U16(static_cast<uint16_t>(status));
                update.U16(static_cast<uint16_t>(width)).U16(static_cast<uint16_t>(height));
                update.S32(Rfb::Encoding::ExtendedDesktopSize);
                update.U8(static_cast<uint8_t>(screens.size())).Padding(3);
                for (const Rfb::Screen& screen : screens)
                {
                    update.U32(screen.id);
                    update.U16(static_cast<uint16_t>(screen.x)).U16(static_cast<uint16_t>(screen.y));
                    update.U16(static_cast<uint16_t>(screen.width)).U16(static_cast<uint16_t>(screen.height));
                    update.U32(screen.flags);
                }
                server.Send(update);
            }
        };

        const std::vector<Rfb::Screen> kTwoScreens = {{7, 0, 0, 120, 100, 0}, {9, 120, 0, 80, 100, 0}};
    } // namespace

    void RunDesktopSizeTests()
    {
        RunTest("The server's display list arrives with the desktop size", []() {
            Session s;
            CHECK(!s.client.SupportsDesktopResize());
            CHECK(s.client.GetScreens().empty());
            s.SendLayout(0, 0, 200, 100, kTwoScreens);
            CHECK(s.listener.WaitFor([&]() { return s.listener.screenLayouts == 1; }));
            CHECK(s.client.SupportsDesktopResize());
            std::vector<Rfb::Screen> screens = s.client.GetScreens();
            CHECK(screens.size() == 2);
            CHECK(screens[0].id == 7 && screens[0].width == 120 && screens[0].height == 100);
            CHECK(screens[1].id == 9 && screens[1].x == 120 && screens[1].width == 80);
            CHECK(s.listener.resizedWidth == 0); // the size didn't change, so no resize was reported
            CHECK(s.client.IsConnected());
        });

        RunTest("A size change with a display list resizes and reports the new list", []() {
            Session s;
            s.SendLayout(2, 0, 300, 150, {{1, 0, 0, 300, 150, 0}});
            CHECK(s.listener.WaitFor([&]() { return s.listener.screenLayouts == 1 && s.listener.resizedWidth == 300; }));
            CHECK(s.listener.resizedHeight == 150);
            CHECK(s.client.GetScreens().size() == 1);
        });

        RunTest("Asking for a new screen size sends SetDesktopSize with one display of that size", []() {
            Session s;
            s.SendLayout(0, 0, 200, 100, kTwoScreens);
            CHECK(s.listener.WaitFor([&]() { return s.listener.screenLayouts == 1; }));
            s.client.RequestDesktopSize(640, 360);
            Rfb::InStream& in = s.server.In();
            CHECK(in.U8() == Rfb::ClientMessage::SetDesktopSize);
            in.Skip(1);
            CHECK(in.U16() == 640 && in.U16() == 360);
            CHECK(in.U8() == 1);
            in.Skip(1);
            CHECK(in.U32() == 7); // the first display's id is kept
            CHECK(in.U16() == 0 && in.U16() == 0 && in.U16() == 640 && in.U16() == 360);
            CHECK(in.U32() == 0);
        });

        RunTest("The message bytes are laid out as the protocol says", []() {
            Rfb::Bytes message = Rfb::Client::BuildSetDesktopSize(1024, 768, {{3, 5, 6, 10, 20, 1}});
            CHECK(message == HexBytes("fb00" "0400" "0300" "01" "00" "00000003" "0000" "0000" "0400" "0300" "00000001"));
            Rfb::Bytes fallback = Rfb::Client::BuildSetDesktopSize(10, 20, {});
            CHECK(fallback == HexBytes("fb00" "000a" "0014" "01" "00" "00000001" "0000" "0000" "000a" "0014" "00000000"));
        });

        RunTest("A refused size change is reported and changes nothing", []() {
            Session s;
            s.SendLayout(0, 0, 200, 100, kTwoScreens);
            CHECK(s.listener.WaitFor([&]() { return s.listener.screenLayouts == 1; }));
            s.SendLayout(1, 3, 200, 100, kTwoScreens); // reason 1: our request; status 3: out of resources
            CHECK(s.listener.WaitFor([&]() { return s.listener.resizeRefusedStatus == 3; }));
            CHECK(s.listener.resizedWidth == 0);
            CHECK(s.client.IsConnected());
        });

        RunTest("Without a display list from the server, size requests aren't sent", []() {
            Session s;
            s.client.RequestDesktopSize(640, 360);
            Rfb::OutBuffer bell;
            bell.U8(Rfb::ServerMessage::Bell);
            s.server.Send(bell);
            CHECK(s.listener.WaitFor([&]() { return s.listener.bells == 1; }));
            s.client.SendKeyEvent(0x41, true); // the next thing the server reads must be this, not a size request
            CHECK(s.server.In().U8() == Rfb::ClientMessage::KeyEvent);
        });
    }
} // namespace MarconiTest
