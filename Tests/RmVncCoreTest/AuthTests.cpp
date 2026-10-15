// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for text conversion and VNC authentication (unit and against the fake server)

#include "TestSupport.h"

#include <RmVncCore/Rfb/Auth.h>
#include <RmVncCore/Rfb/Messages.h>
#include <RmVncCore/Text.h>

#include <Crypto/Software/Software.h>

namespace RmVncTest
{
    using namespace RmVnc;

    namespace
    {
        const Rfb::Bytes kChallenge = HexBytes("00112233445566778899aabbccddeeff");

        // Starts a client against a fake server and runs the server side up to sending the VNC auth challenge.
        struct AuthSession
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};

            AuthSession(const std::string& initialPassword, const std::string& promptPassword)
            {
                listener.credentialsToGive.password = promptPassword;
                Rfb::ConnectionOptions options;
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                options.credentials.password = initialPassword;
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
                server.SendString("RFB 003.008\n");
                server.In().ReadString(12);
                Rfb::OutBuffer types;
                types.U8(2).U8(Rfb::SecurityType::VncAuth).U8(Rfb::SecurityType::Tight);
                server.Send(types);
                CHECK(server.In().U8() == Rfb::SecurityType::VncAuth);
                server.Send(kChallenge);
            }
        };
    } // namespace

    void RunTextTests()
    {
        RunTest("Latin-1 / UTF-8 conversion", []() {
            CHECK(Utf8ToLatin1("plain") == "plain");
            CHECK(Utf8ToLatin1("caf\xc3\xa9") == "caf\xe9");
            CHECK(Utf8ToLatin1("\xe2\x82\xac") == "?");       // euro sign isn't Latin-1
            CHECK(Utf8ToLatin1("\xf0\x9f\x98\x80") == "?");   // emoji
            CHECK(Utf8ToLatin1("bad\xe9x") == "bad\xe9x");    // invalid UTF-8 passes through
            CHECK(Latin1ToUtf8("caf\xe9") == "caf\xc3\xa9");
            CHECK(Latin1ToUtf8(Utf8ToLatin1("\xc3\xbf")) == "\xc3\xbf");
            CHECK(ToNetworkLineEndings("a\r\nb\nc\r") == "a\nb\nc\r");
        });
    }

    void RunAuthTests()
    {
        RunTest("VNC auth response mirrors key bits", []() {
            // "password" with each byte bit-reversed, worked out by hand: p=70->0e a=61->86 s=73->ce w=77->ee
            // o=6f->f6 r=72->4e d=64->26.
            const uint8_t key[8] = {0x0e, 0x86, 0xce, 0xce, 0xee, 0xf6, 0x4e, 0x26};
            Rfb::Bytes expected(16);
            Crypto::Software::DesEcbEncrypt(key, kChallenge.data(), expected.data(), 16);
            CHECK(Rfb::ComputeVncAuthResponse("password", kChallenge) == expected);

            // Only the first 8 characters count, and short passwords are zero padded.
            CHECK(Rfb::ComputeVncAuthResponse("password-and-more", kChallenge) == expected);
            const uint8_t shortKey[8] = {0x86, 0x46, 0xc6, 0, 0, 0, 0, 0}; // "abc"
            Crypto::Software::DesEcbEncrypt(shortKey, kChallenge.data(), expected.data(), 16);
            CHECK(Rfb::ComputeVncAuthResponse("abc", kChallenge) == expected);
        });

        RunTest("VNC auth with a supplied password", []() {
            AuthSession s("secret", "");
            CHECK(s.server.In().ReadBytes(16) == Rfb::ComputeVncAuthResponse("secret", kChallenge));
            Rfb::OutBuffer ok;
            ok.U32(0);
            s.server.Send(ok);
            CHECK(s.server.In().U8() == 1); // ClientInit
            CHECK(s.listener.credentialRequests == 0);
        });

        RunTest("VNC auth prompts when no password was supplied", []() {
            AuthSession s("", "prompted");
            CHECK(s.server.In().ReadBytes(16) == Rfb::ComputeVncAuthResponse("prompted", kChallenge));
            CHECK(s.listener.credentialRequests == 1);
            CHECK(!s.listener.lastNeedUsername);
        });

        RunTest("Rejected password reports an authentication failure", []() {
            AuthSession s("wrong", "");
            s.server.In().ReadBytes(16);
            Rfb::OutBuffer failed;
            failed.U32(1).U32(19).Append("Password incorrect.");
            s.server.Send(failed);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.authenticationFailed);
            CHECK(s.listener.disconnectError == "Password incorrect.");
        });

        RunTest("Cancelling the password prompt disconnects quietly", []() {
            AuthSession s("", ""); // listener returns false: cancelled
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.empty());
            CHECK(!s.listener.authenticationFailed);
            CHECK(s.listener.credentialRequests == 1);
        });
    }
} // namespace RmVncTest
