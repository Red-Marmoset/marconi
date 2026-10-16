// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for text conversion and VNC authentication (unit and against the fake server)

#include "TestSupport.h"

#include <MarconiCore/Rfb/Auth.h>
#include <MarconiCore/Rfb/Messages.h>
#include <MarconiCore/Text.h>

#include <Crypto/Crypto.h>
#include <Crypto/Software/Software.h>

#include <cstring>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        const Rfb::Bytes kChallenge = HexBytes("00112233445566778899aabbccddeeff");

        // RFC 2409 Oakley group 2, the 1024-bit MODP group (macOS uses a 128 byte key too).
        const Rfb::Bytes kArdPrime = HexBytes(
            "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD"
            "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
            "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF");

        // Server side of Apple authentication: decrypts what the client sent and returns username/password.
        bool DecryptArd(const Crypto::DhKeyPair& serverKeys, const Rfb::Bytes& response, std::string& username,
                        std::string& password)
        {
            if (response.size() != 128 + kArdPrime.size())
            {
                return false;
            }
            Rfb::Bytes clientPublic(response.begin() + 128, response.end());
            Rfb::Bytes secret;
            if (!Crypto::DhComputeSharedSecret(kArdPrime, serverKeys, clientPublic, secret))
            {
                return false;
            }
            uint8_t key[16];
            Crypto::Md5(secret.data(), secret.size(), key);
            uint8_t plain[128];
            Crypto::Aes128EcbDecrypt(key, response.data(), plain, 128);
            username.assign(reinterpret_cast<const char*>(plain), strnlen(reinterpret_cast<const char*>(plain), 64));
            password.assign(reinterpret_cast<const char*>(plain + 64), strnlen(reinterpret_cast<const char*>(plain + 64), 64));
            return true;
        }

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

        RunTest("Hex conversion for saved fingerprints", []() {
            CHECK(BytesToHex({0x00, 0xab, 0xff}) == "00abff");
            CHECK(BytesToHex({}).empty());
            CHECK(HexToBytes("00abff") == std::vector<uint8_t>({0x00, 0xab, 0xff}));
            CHECK(HexToBytes("00:AB:FF") == std::vector<uint8_t>({0x00, 0xab, 0xff}));
            CHECK(HexToBytes("abc").empty());  // odd length
            CHECK(HexToBytes("zz").empty());   // not hex
            CHECK(HexToBytes("").empty());
            std::vector<uint8_t> fingerprint(32, 0x5a);
            CHECK(HexToBytes(BytesToHex(fingerprint)) == fingerprint);
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

        RunTest("Apple ARD response decrypts to the credentials", []() {
            Crypto::DhKeyPair server;
            CHECK(Crypto::DhGenerateKeyPair(kArdPrime, {0, 2}, server));
            Rfb::Credentials credentials{"sem", "hunter2-\xc3\xa9"}; // non-ASCII is sent as UTF-8
            Rfb::Bytes response = Rfb::ComputeArdResponse({0, 2}, kArdPrime, server.publicKey, credentials);
            std::string username;
            std::string password;
            CHECK(DecryptArd(server, response, username, password));
            CHECK(username == "sem");
            CHECK(password == "hunter2-\xc3\xa9");

            // Long names are cut at 63 bytes without splitting a UTF-8 character.
            std::string longName(62, 'a');
            longName += "\xc3\xa9"; // 64 bytes: the two-byte character doesn't fit
            Rfb::Bytes truncated =
                Rfb::ComputeArdResponse({0, 2}, kArdPrime, server.publicKey, {longName, "x"});
            CHECK(DecryptArd(server, truncated, username, password));
            CHECK(username == std::string(62, 'a'));

            // Two responses differ (fresh DH keys and random padding) but both decrypt.
            Rfb::Bytes again = Rfb::ComputeArdResponse({0, 2}, kArdPrime, server.publicKey, credentials);
            CHECK(again != response);
            CHECK(DecryptArd(server, again, username, password) && username == "sem");

            // A degenerate server key is rejected.
            bool threw = false;
            try
            {
                Rfb::ComputeArdResponse({0, 2}, kArdPrime, Rfb::Bytes{1}, credentials);
            }
            catch (const Rfb::RfbError&)
            {
                threw = true;
            }
            CHECK(threw);
        });

        RunTest("Apple ARD against the fake server, chosen when a user name is given", []() {
            FakeServer server;
            RecordingListener listener;
            listener.credentialsToGive = {"admin", "secret"};
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.credentials.username = "admin";
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.SendString("RFB 003.889\n");
            CHECK(server.In().ReadString(12) == "RFB 003.008\n");
            Rfb::OutBuffer types;
            types.U8(2).U8(Rfb::SecurityType::VncAuth).U8(Rfb::SecurityType::AppleRemoteDesktop);
            server.Send(types);
            CHECK(server.In().U8() == Rfb::SecurityType::AppleRemoteDesktop);

            Crypto::DhKeyPair keys;
            CHECK(Crypto::DhGenerateKeyPair(kArdPrime, {0, 2}, keys));
            Rfb::OutBuffer parameters;
            parameters.U16(2).U16(static_cast<uint16_t>(kArdPrime.size()));
            parameters.Append(kArdPrime.data(), kArdPrime.size());
            parameters.Append(keys.publicKey.data(), keys.publicKey.size());
            server.Send(parameters);

            Rfb::Bytes response = server.In().ReadBytes(128 + kArdPrime.size());
            std::string username;
            std::string password;
            CHECK(DecryptArd(keys, response, username, password));
            CHECK(username == "admin" && password == "secret");
            CHECK(listener.credentialRequests == 1 && listener.lastKind == Rfb::CredentialsKind::PasswordOrAccount);

            Rfb::OutBuffer ok;
            ok.U32(0);
            server.Send(ok);
            CHECK(server.In().U8() == 1); // ClientInit
        });

        RunTest("A Mac offering both logins: no user name at the prompt means the VNC password", []() {
            FakeServer server;
            RecordingListener listener;
            listener.credentialsToGive = {"", "secret"};
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.SendString("RFB 003.889\n");
            CHECK(server.In().ReadString(12) == "RFB 003.008\n");
            Rfb::OutBuffer types;
            types.U8(2).U8(Rfb::SecurityType::AppleRemoteDesktop).U8(Rfb::SecurityType::VncAuth);
            server.Send(types);
            CHECK(server.In().U8() == Rfb::SecurityType::VncAuth);
            Rfb::Bytes challenge(16, 0x5a);
            server.Send(challenge);
            CHECK(server.In().ReadBytes(16) == Rfb::ComputeVncAuthResponse("secret", challenge));
            CHECK(listener.credentialRequests == 1 && listener.lastKind == Rfb::CredentialsKind::PasswordOrAccount);
        });

        RunTest("A Mac offering both logins: a saved VNC password doesn't prompt", []() {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.credentials.password = "saved";
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.SendString("RFB 003.889\n");
            CHECK(server.In().ReadString(12) == "RFB 003.008\n");
            Rfb::OutBuffer types;
            types.U8(2).U8(Rfb::SecurityType::AppleRemoteDesktop).U8(Rfb::SecurityType::VncAuth);
            server.Send(types);
            CHECK(server.In().U8() == Rfb::SecurityType::VncAuth);
            Rfb::Bytes challenge(16, 0x11);
            server.Send(challenge);
            CHECK(server.In().ReadBytes(16) == Rfb::ComputeVncAuthResponse("saved", challenge));
            CHECK(listener.credentialRequests == 0);
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
} // namespace MarconiTest
