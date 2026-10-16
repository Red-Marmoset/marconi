// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for encrypted connections: VeNCrypt negotiation, switching the connection to the (fake) encrypted stream,
// certificate trust decisions and the handshake timeout

#include "TestSupport.h"

#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rfb/Auth.h>
#include <MarconiCore/Rfb/Handshake.h>
#include <MarconiCore/Rfb/Messages.h>

#include <chrono>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        constexpr uint8_t kMask = 0x5a;
        const std::string kHello = "TLS?";
        const std::string kAgreed = "TLS!";

        // Stands in for TLS: a four byte handshake, then every byte XORed with a mask, so the tests can tell that
        // both directions really go through the stream once it is in place. A non-empty certificate plays the part of
        // one this computer doesn't trust, so the trust decision is asked for.
        class FakeTls : public Net::TlsStream
        {
        public:
            FakeTls(Net::Socket& socket, Net::TlsOptions options, Net::TlsCertificate certificate)
                : m_socket(socket), m_options(std::move(options)), m_certificate(std::move(certificate))
            {
            }

            bool Handshake(std::string& error) override
            {
                if (!m_socket.SendAll(kHello.data(), kHello.size()))
                {
                    error = "could not send the hello";
                    return false;
                }
                std::string reply;
                while (reply.size() < kAgreed.size())
                {
                    char buffer[4];
                    ptrdiff_t got = m_socket.Receive(buffer, kAgreed.size() - reply.size());
                    if (got <= 0)
                    {
                        error = "no reply to the hello";
                        return false;
                    }
                    reply.append(buffer, static_cast<size_t>(got));
                }
                if (reply != kAgreed)
                {
                    error = "unexpected reply to the hello";
                    return false;
                }
                if (!m_certificate.sha256.empty() &&
                    (!m_options.trustDecision || !m_options.trustDecision(m_certificate)))
                {
                    error = "the certificate was not accepted";
                    return false;
                }
                return true;
            }

            ptrdiff_t Read(void* buffer, size_t length) override
            {
                ptrdiff_t got = m_socket.Receive(buffer, length);
                uint8_t* bytes = static_cast<uint8_t*>(buffer);
                for (ptrdiff_t i = 0; i < got; ++i)
                {
                    bytes[i] ^= kMask;
                }
                return got;
            }

            bool Write(const void* data, size_t length) override
            {
                const uint8_t* bytes = static_cast<const uint8_t*>(data);
                std::vector<uint8_t> masked(length);
                for (size_t i = 0; i < length; ++i)
                {
                    masked[i] = bytes[i] ^ kMask;
                }
                return m_socket.SendAll(masked.data(), masked.size());
            }

            void Shutdown() override {}
            std::string GetProtocolName() const override { return "Fake TLS"; }

        private:
            Net::Socket& m_socket;
            Net::TlsOptions m_options;
            Net::TlsCertificate m_certificate;
        };

        // The server side of FakeTls's masking.
        class MaskedSource : public Rfb::ByteSource
        {
        public:
            explicit MaskedSource(Net::Socket& socket) : m_socket(socket) {}
            ptrdiff_t Read(void* buffer, size_t length) override
            {
                ptrdiff_t got = m_socket.Receive(buffer, length);
                uint8_t* bytes = static_cast<uint8_t*>(buffer);
                for (ptrdiff_t i = 0; i < got; ++i)
                {
                    bytes[i] ^= kMask;
                }
                return got;
            }

        private:
            Net::Socket& m_socket;
        };

        // A client connected to a fake server whose encryption is FakeTls, taken as far as the version banners.
        struct VeNCryptSession
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};

            explicit VeNCryptSession(Rfb::ConnectionOptions options = {}, Net::TlsCertificate certificate = {})
            {
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                options.createTlsStream = [certificate](Net::Socket& socket, const Net::TlsOptions& tlsOptions) {
                    return std::unique_ptr<Net::TlsStream>(new FakeTls(socket, tlsOptions, certificate));
                };
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
                server.SendString("RFB 003.008\n");
                server.In().ReadString(12);
            }

            // Server side of the VeNCrypt exchange. `expectChosen` is the subtype the client should pick (0: it
            // should give up on the list), `versionAck` what the server says to the client's version and `startAck`
            // to its choice. When encryption starts, the server switches to the masked stream too.
            void OfferVeNCrypt(const std::vector<uint32_t>& subtypes, uint32_t expectChosen, uint8_t versionAck = 0,
                               uint8_t startAck = 1)
            {
                Rfb::OutBuffer types;
                types.U8(1).U8(Rfb::SecurityType::VeNCrypt);
                server.Send(types);
                CHECK(server.In().U8() == Rfb::SecurityType::VeNCrypt);
                Rfb::OutBuffer version;
                version.U8(0).U8(2);
                server.Send(version);
                CHECK(server.In().U8() == 0 && server.In().U8() == 2);
                Rfb::OutBuffer ack;
                ack.U8(versionAck);
                server.Send(ack);
                if (versionAck != 0)
                {
                    return;
                }
                Rfb::OutBuffer list;
                list.U8(static_cast<uint8_t>(subtypes.size()));
                for (uint32_t subtype : subtypes)
                {
                    list.U32(subtype);
                }
                server.Send(list);
                if (expectChosen == 0)
                {
                    return;
                }
                CHECK(server.In().U32() == expectChosen);
                Rfb::OutBuffer start;
                start.U8(startAck);
                server.Send(start);
                if (startAck != 1)
                {
                    return;
                }
                CHECK(server.In().ReadString(kHello.size()) == kHello);
                server.SendString(kAgreed);
                Net::Socket& socket = server.GetClientSocket();
                server.Reroute(std::make_unique<MaskedSource>(socket), [&socket](const Rfb::Bytes& data) {
                    std::vector<uint8_t> masked(data.size());
                    for (size_t i = 0; i < data.size(); ++i)
                    {
                        masked[i] = data[i] ^ kMask;
                    }
                    if (!socket.SendAll(masked.data(), masked.size()))
                    {
                        throw Rfb::RfbError("FakeServer masked send failed");
                    }
                });
            }

            // SecurityResult OK, then ServerInit, then the client's setup messages.
            void FinishHandshake(const std::string& name)
            {
                Rfb::OutBuffer ok;
                ok.U32(0);
                server.Send(ok);
                CHECK(server.In().U8() == 1); // ClientInit
                Rfb::OutBuffer init;
                init.U16(8).U16(8);
                Rfb::PixelFormat().Write(init);
                init.U32(static_cast<uint32_t>(name.size())).Append(name);
                server.Send(init);
                server.ReadClientSetup();
            }
        };

        bool Disconnected(VeNCryptSession& s)
        {
            return s.listener.WaitFor([&]() { return s.listener.disconnected; });
        }
    } // namespace

    void RunVeNCryptTests()
    {
        RunTest("Encryption is chosen first, but only when this build can make it", []() {
            using namespace Rfb::SecurityType;
            CHECK(Rfb::ChooseSecurityType({None, VeNCrypt}) == None);
            CHECK(Rfb::ChooseSecurityType({None, VeNCrypt}, false, true) == VeNCrypt);
            CHECK(Rfb::ChooseSecurityType({VncAuth, VeNCrypt, AppleRemoteDesktop}, true, true) == VeNCrypt);
            CHECK(Rfb::ChooseSecurityType({VeNCrypt}) == Invalid);
            CHECK(Rfb::ChooseSecurityType({VncAuth}, false, true) == VncAuth);
        });

        RunTest("VeNCrypt subtype choice: a certificate first, no login before a password, plain text never", []() {
            using namespace Rfb::VeNCrypt;
            CHECK(Rfb::ChooseVeNCryptSubtype({TlsVnc, X509Vnc}) == X509Vnc);
            CHECK(Rfb::ChooseVeNCryptSubtype({X509Plain, X509Vnc}) == X509Vnc);
            CHECK(Rfb::ChooseVeNCryptSubtype({X509Plain, X509None}) == X509None);
            CHECK(Rfb::ChooseVeNCryptSubtype({TlsPlain, TlsVnc}) == TlsVnc);
            CHECK(Rfb::ChooseVeNCryptSubtype({TlsPlain}) == TlsPlain);
            CHECK(Rfb::ChooseVeNCryptSubtype({Plain}) == 0);
            CHECK(Rfb::ChooseVeNCryptSubtype({Plain, 999}) == 0);
            CHECK(Rfb::ChooseVeNCryptSubtype({}) == 0);
        });

        RunTest("VeNCrypt: the VNC password and everything after it go through the encrypted stream", []() {
            Rfb::ConnectionOptions options;
            options.credentials.password = "secret";
            VeNCryptSession s(options);
            s.OfferVeNCrypt({Rfb::VeNCrypt::Plain, Rfb::VeNCrypt::TlsVnc, Rfb::VeNCrypt::X509Vnc},
                            Rfb::VeNCrypt::X509Vnc);
            Rfb::Bytes challenge(16, 0x33);
            s.server.Send(challenge);
            CHECK(s.server.In().ReadBytes(16) == Rfb::ComputeVncAuthResponse("secret", challenge));
            s.FinishHandshake("safe");
            CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
            CHECK(s.listener.serverInit.name == "safe");
            CHECK(s.client.IsEncrypted());
            CHECK(s.client.GetEncryptionName() == "Fake TLS");
            CHECK(s.listener.certificateQuestions == 0);

            // Input from the UI thread is encrypted too.
            s.client.SendKeyEvent(0xff0d, true);
            CHECK(s.server.In().U8() == Rfb::ClientMessage::KeyEvent);
            CHECK(s.server.In().U8() == 1);
        });

        RunTest("VeNCrypt: a user name and password login over anonymous encryption", []() {
            VeNCryptSession s;
            s.listener.credentialsToGive = {"alice", "pw"};
            s.OfferVeNCrypt({Rfb::VeNCrypt::TlsPlain}, Rfb::VeNCrypt::TlsPlain);
            CHECK(s.server.In().U32() == 5);
            CHECK(s.server.In().U32() == 2);
            CHECK(s.server.In().ReadString(5) == "alice");
            CHECK(s.server.In().ReadString(2) == "pw");
            CHECK(s.listener.lastKind == Rfb::CredentialsKind::Account);
            s.FinishHandshake("plain");
            CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
            CHECK(s.client.IsEncrypted());
        });

        RunTest("VeNCrypt: no login needed after encryption", []() {
            VeNCryptSession s;
            s.OfferVeNCrypt({Rfb::VeNCrypt::X509None}, Rfb::VeNCrypt::X509None);
            s.FinishHandshake("open");
            CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
            CHECK(s.listener.credentialRequests == 0);
            CHECK(s.client.IsEncrypted());
        });

        RunTest("A plain connection reports itself as not encrypted", []() {
            VeNCryptSession s;
            Rfb::OutBuffer types;
            types.U8(1).U8(Rfb::SecurityType::None);
            s.server.Send(types);
            CHECK(s.server.In().U8() == Rfb::SecurityType::None);
            s.FinishHandshake("plain");
            CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
            CHECK(!s.client.IsEncrypted());
            CHECK(s.client.GetEncryptionName().empty());
        });

        RunTest("VeNCrypt refusals are explained", []() {
            {
                VeNCryptSession s;
                s.OfferVeNCrypt({}, 0, 1); // the server rejects our version
                CHECK(Disconnected(s));
                CHECK(s.listener.disconnectError.find("VeNCrypt version") != std::string::npos);
            }
            {
                VeNCryptSession s;
                s.OfferVeNCrypt({}, 0); // nothing on offer
                CHECK(Disconnected(s));
                CHECK(s.listener.disconnectError.find("no encrypted login method") != std::string::npos);
            }
            {
                VeNCryptSession s;
                s.OfferVeNCrypt({Rfb::VeNCrypt::Plain}, 0); // unencrypted password: never
                CHECK(Disconnected(s));
                CHECK(s.listener.disconnectError.find("256") != std::string::npos);
            }
            {
                VeNCryptSession s;
                s.OfferVeNCrypt({Rfb::VeNCrypt::X509None}, Rfb::VeNCrypt::X509None, 0, 0);
                CHECK(Disconnected(s));
                CHECK(s.listener.disconnectError.find("couldn't start encryption") != std::string::npos);
            }
        });

        RunTest("A server that only offers encryption is explained when this build can't encrypt", []() {
            if (Net::IsTlsAvailable())
            {
                printf("  (skipped: this build can encrypt)\n");
                return;
            }
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.SendString("RFB 003.008\n");
            server.In().ReadString(12);
            Rfb::OutBuffer types;
            types.U8(1).U8(Rfb::SecurityType::VeNCrypt);
            server.Send(types);
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            CHECK(listener.disconnectError.find("encrypted connection") != std::string::npos);
            CHECK(listener.disconnectError.find("19") != std::string::npos);
        });

        RunTest("An untrusted certificate is refused unless the listener or a remembered fingerprint accepts it", []() {
            Net::TlsCertificate certificate;
            certificate.sha256.assign(32, 0xab);
            certificate.subject = "CN=studio-mac.local";
            certificate.problem = "self-signed";
            {
                // Declined: a quiet disconnect, like cancelling a password prompt.
                VeNCryptSession s({}, certificate);
                s.OfferVeNCrypt({Rfb::VeNCrypt::X509None}, Rfb::VeNCrypt::X509None);
                CHECK(Disconnected(s));
                CHECK(s.listener.certificateQuestions == 1);
                CHECK(s.listener.lastCertificate.subject == "CN=studio-mac.local");
                CHECK(s.listener.disconnectError.empty());
                CHECK(!s.listener.connected);
            }
            {
                VeNCryptSession s({}, certificate);
                s.listener.trustCertificate = true;
                s.OfferVeNCrypt({Rfb::VeNCrypt::X509None}, Rfb::VeNCrypt::X509None);
                s.FinishHandshake("trusted");
                CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
                CHECK(s.listener.certificateQuestions == 1);
            }
            {
                Rfb::ConnectionOptions options;
                options.trustedCertificateSha256 = certificate.sha256;
                VeNCryptSession s(options, certificate);
                s.OfferVeNCrypt({Rfb::VeNCrypt::X509None}, Rfb::VeNCrypt::X509None);
                s.FinishHandshake("remembered");
                CHECK(s.listener.WaitFor([&]() { return s.listener.connected; }));
                CHECK(s.listener.certificateQuestions == 0);
            }
        });

        RunTest("A server that accepts the connection but never answers is reported as not responding", []() {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.connectTimeoutMs = 400;
            client.Connect(options);
            CHECK(server.AcceptClient());
            using Clock = std::chrono::steady_clock;
            Clock::time_point start = Clock::now();
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
            CHECK(elapsed.count() < 3000);
            CHECK(listener.disconnectError.find("stopped responding") != std::string::npos);
            CHECK(!listener.connected);
        });

        RunTest("The handshake timeout doesn't apply once connected", []() {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.connectTimeoutMs = 300;
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.HandshakeNone("RFB 003.008\n", 4, 3, "idle");
            server.ReadClientSetup();
            CHECK(listener.WaitFor([&]() { return listener.connected; }));
            // A quiet server is normal once connected: nothing changes on screen, nothing is sent.
            CHECK(!listener.WaitFor([&]() { return listener.disconnected; }, 900));
            CHECK(client.IsConnected());
        });
    }
} // namespace MarconiTest
