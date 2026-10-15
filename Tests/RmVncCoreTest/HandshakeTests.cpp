// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RFB version parsing, security type choice and the handshake against a fake server

#include "TestSupport.h"

#include <RmVncCore/Rfb/Handshake.h>
#include <RmVncCore/Rfb/Messages.h>

namespace RmVncTest
{
    using namespace RmVnc;

    namespace
    {
        // Connects a Client to a fresh FakeServer and returns once the server has accepted.
        struct Session
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};

            explicit Session(Rfb::Credentials credentials = {})
            {
                Rfb::ConnectionOptions options;
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                options.credentials = credentials;
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
            }
        };
    } // namespace

    void RunHandshakeTests()
    {
        RunTest("ParseVersion", []() {
            int major = 0;
            int minor = 0;
            CHECK(Rfb::ParseVersion("RFB 003.008\n", major, minor) && major == 3 && minor == 8);
            CHECK(Rfb::ParseVersion("RFB 003.889\n", major, minor) && minor == 889);
            CHECK(!Rfb::ParseVersion("RFB 003.008", major, minor));
            CHECK(!Rfb::ParseVersion("HTTP/1.1 200", major, minor));
            CHECK(!Rfb::ParseVersion("RFB 00x.008\n", major, minor));
        });

        RunTest("ChooseSecurityType", []() {
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::None}) == Rfb::SecurityType::None);
            CHECK(Rfb::ChooseSecurityType({99, Rfb::SecurityType::None}) == Rfb::SecurityType::None);
            CHECK(Rfb::ChooseSecurityType({99, 100}) == Rfb::SecurityType::Invalid);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::VncAuth, Rfb::SecurityType::Tight}) ==
                  Rfb::SecurityType::VncAuth);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::VncAuth, Rfb::SecurityType::None}) ==
                  Rfb::SecurityType::None);
            // A Mac offering both: the VNC password by default, the account login when asked for.
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::AppleRemoteDesktop, Rfb::SecurityType::VncAuth}) ==
                  Rfb::SecurityType::VncAuth);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::AppleRemoteDesktop, Rfb::SecurityType::VncAuth}, true) ==
                  Rfb::SecurityType::AppleRemoteDesktop);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::AppleRemoteDesktop}) ==
                  Rfb::SecurityType::AppleRemoteDesktop);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::VncAuth}, true) == Rfb::SecurityType::VncAuth);
            CHECK(Rfb::ChooseSecurityType({}) == Rfb::SecurityType::Invalid);
        });

        RunTest("Handshake 3.8 with security None", []() {
            Session s;
            s.server.HandshakeNone("RFB 003.008\n", 64, 32, "test desktop");
            std::vector<int32_t> encodings = s.server.ReadClientSetup();
            CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
            CHECK(s.listener.serverInit.width == 64 && s.listener.serverInit.height == 32);
            CHECK(s.listener.serverInit.name == "test desktop");
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::Raw) != encodings.end());
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::DesktopSize) != encodings.end());
            CHECK(s.client.GetFramebuffer().GetWidth() == 64);
        });

        RunTest("Client answers 3.8 to Apple's 3.889 and 3.3 to 3.3", []() {
            {
                Session s;
                s.server.SendString("RFB 003.889\n");
                CHECK(s.server.In().ReadString(12) == "RFB 003.008\n");
            }
            {
                Session s;
                s.server.SendString("RFB 003.003\n");
                CHECK(s.server.In().ReadString(12) == "RFB 003.003\n");
                // 3.3: server picks the type as a u32, and there is no SecurityResult for None.
                Rfb::OutBuffer type;
                type.U32(Rfb::SecurityType::None);
                s.server.Send(type);
                CHECK(s.server.In().U8() == 1); // ClientInit shared
                Rfb::OutBuffer init;
                init.U16(8).U16(8);
                Rfb::PixelFormat().Write(init);
                init.U32(2).Append("33");
                s.server.Send(init);
                s.server.ReadClientSetup();
                CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
                CHECK(s.listener.serverInit.name == "33");
            }
        });

        RunTest("Server refusal reason is reported", []() {
            Session s;
            s.server.SendString("RFB 003.008\n");
            s.server.In().ReadString(12);
            Rfb::OutBuffer refusal;
            refusal.U8(0).U32(9).Append("Go away!!");
            s.server.Send(refusal);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("Go away!!") != std::string::npos);
            CHECK(!s.listener.connected);
        });

        RunTest("Unsupported security types give a helpful error", []() {
            Session s;
            s.server.SendString("RFB 003.008\n");
            s.server.In().ReadString(12);
            Rfb::OutBuffer types;
            types.U8(2).U8(200).U8(201);
            s.server.Send(types);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("200, 201") != std::string::npos);
        });

        RunTest("Non-VNC server is rejected", []() {
            Session s;
            s.server.SendString("HTTP/1.1 400 Bad Request\r\n");
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("doesn't look like a VNC server") != std::string::npos);
        });
    }
} // namespace RmVncTest
