// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RFB version parsing, security type choice and the handshake against a fake server

#include "TestSupport.h"

#include <MarconiCore/Rfb/Handshake.h>
#include <MarconiCore/Rfb/Messages.h>

namespace MarconiTest
{
    using namespace Marconi;

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

        class DiscardSink : public Rfb::ByteSink
        {
        public:
            void Send(const Rfb::Bytes&) override {}
        };

        // Runs the client side of the handshake against `serverBytes`, cancelling any request for credentials.
        Rfb::HandshakeResult Handshake(const Rfb::Bytes& serverBytes)
        {
            Rfb::MemorySource source(serverBytes);
            Rfb::InStream in(source);
            DiscardSink sink;
            return Rfb::PerformHandshake(in, sink, true, [](Rfb::CredentialsKind, Rfb::Credentials&) { return false; });
        }

        bool HandshakeFails(const Rfb::Bytes& serverBytes, const std::string& messagePart)
        {
            return ThrowsRfbError([&]() { Handshake(serverBytes); }, messagePart);
        }

        // A 3.8 server that refuses the connection with `reason`, sent with `length` as its length.
        Rfb::Bytes Refusal(const std::string& reason, uint32_t length)
        {
            Rfb::OutBuffer out;
            out.Append("RFB 003.008\n").U8(0).U32(length).Append(reason);
            return out.Data();
        }

        // A 3.8 server with no security, up to ServerInit with a desktop name of `nameLength` bytes.
        Rfb::Bytes ServerInitWithName(uint32_t nameLength)
        {
            Rfb::OutBuffer out;
            out.Append("RFB 003.008\n").U8(1).U8(Rfb::SecurityType::None).U32(0);
            out.U16(8).U16(8);
            Rfb::PixelFormat().Write(out);
            out.U32(nameLength).Append(std::string(nameLength, 'n'));
            return out.Data();
        }

        // A 3.8 server offering only Apple's login, sending DH parameters with `keyLength` as their length.
        Rfb::Bytes ArdParameters(uint16_t keyLength, size_t keyBytes)
        {
            Rfb::OutBuffer out;
            out.Append("RFB 003.008\n").U8(1).U8(Rfb::SecurityType::AppleRemoteDesktop);
            out.U16(2).U16(keyLength).Append(std::string(keyBytes * 2, '\x7f'));
            return out.Data();
        }
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

        RunTest("Refusal reasons over 64 KiB are refused", []() {
            const uint32_t limit = 64 * 1024;
            CHECK(HandshakeFails(Refusal(std::string(limit + 1, 'r'), limit + 1),
                                 "The server sent an invalid error message"));
            CHECK(HandshakeFails(Refusal("", 0xFFFFFFFFu), "The server sent an invalid error message"));
            CHECK(HandshakeFails(Refusal(std::string(limit, 'r'), limit), "The server refused the connection: rrr"));
        });

        RunTest("Desktop names over 64 KiB are refused", []() {
            const uint32_t limit = 64 * 1024;
            CHECK(HandshakeFails(ServerInitWithName(limit + 1), "The server sent an invalid desktop name"));
            CHECK(Handshake(ServerInitWithName(limit)).serverInit.name.size() == limit);
        });

        RunTest("RFB 3.3 refusal (security type 0) reports the reason", []() {
            Rfb::OutBuffer server;
            server.Append("RFB 003.003\n").U32(Rfb::SecurityType::Invalid).U32(15).Append("Too many people");
            CHECK(HandshakeFails(server.Data(), "The server refused the connection: Too many people"));
        });

        RunTest("Apple login key lengths outside 16 to 1024 are refused", []() {
            for (int keyLength : {0, 15, 1025, 65535})
            {
                CHECK(HandshakeFails(ArdParameters(static_cast<uint16_t>(keyLength), 0),
                                     "invalid Apple authentication key length"));
            }
            // At the limits the parameters are read and the user is asked to log in (and cancels here).
            for (int keyLength : {16, 1024})
            {
                CHECK(HandshakeFails(ArdParameters(static_cast<uint16_t>(keyLength), static_cast<size_t>(keyLength)),
                                     "Authentication cancelled"));
            }
        });

        RunTest("Non-VNC server is rejected", []() {
            Session s;
            s.server.SendString("HTTP/1.1 400 Bad Request\r\n");
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("doesn't look like a VNC server") != std::string::npos);
        });
    }
} // namespace MarconiTest
