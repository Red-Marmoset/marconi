// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for connecting through SSH: failing cleanly without an SSH server, and (against a real one on this computer,
// opted into with an environment variable) a whole VNC session through the tunnel

#include "TestSupport.h"

#include <MarconiCore/Rfb/Messages.h>

#include <cstdlib>
#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        struct SshLogin
        {
            std::string user;
            std::string password;
            std::string host;
            uint16_t port = 22;
        };

        // MARCONI_SSH_TEST=user:password@host:port. The SSH server must run on this computer, since the tunnel's
        // far end connects to 127.0.0.1 where the fake VNC server listens.
        bool ReadSshLogin(SshLogin& out)
        {
            const char* text = std::getenv("MARCONI_SSH_TEST");
            if (text == nullptr)
            {
                return false;
            }
            std::string value = text;
            size_t colon = value.find(':');
            size_t at = value.rfind('@');
            size_t portColon = value.rfind(':');
            if (colon == std::string::npos || at == std::string::npos || portColon < at)
            {
                printf("MARCONI_SSH_TEST should be user:password@host:port\n");
                return false;
            }
            out.user = value.substr(0, colon);
            out.password = value.substr(colon + 1, at - colon - 1);
            out.host = value.substr(at + 1, portColon - at - 1);
            out.port = static_cast<uint16_t>(std::atoi(value.c_str() + portColon + 1));
            return true;
        }

        Rfb::ConnectionOptions TunnelOptions(const SshLogin& login, uint16_t vncPort)
        {
            Rfb::ConnectionOptions options;
            options.host = login.host;
            options.port = vncPort;
            options.sshTunnel = true;
            options.sshPort = login.port;
            options.credentials.username = login.user;
            options.credentials.password = login.password;
            return options;
        }
    } // namespace

    void RunSshTunnelTests()
    {
        RunTest("Connecting through SSH to something that isn't an SSH server fails with a reason", []() {
            FakeServer notSsh; // a VNC server: its banner isn't SSH's
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = 5900;
            options.sshTunnel = true;
            options.sshPort = notSsh.GetPort();
            options.connectTimeoutMs = 2000; // the SSH handshake waits this long for a banner that never comes
            options.credentials.username = "someone";
            options.credentials.password = "secret";
            client.Connect(options);
            CHECK(notSsh.AcceptClient());
            notSsh.SendString("RFB 003.008\n");
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }, 15000));
            CHECK(!listener.connected);
            CHECK(!listener.disconnectError.empty());
            CHECK(listener.sshHostKeyQuestions == 0);
            CHECK(!client.IsEncrypted());
        });

        SshLogin login;
        if (!ReadSshLogin(login))
        {
            printf("  (set MARCONI_SSH_TEST=user:password@host:port, an SSH server on this computer, for the rest)\n");
            return;
        }

        std::string fingerprint;
        RunTest("A VNC session through SSH: the host key is asked about, then everything flows through the tunnel", [&]() {
            FakeServer server;
            RecordingListener listener;
            listener.trustSshHostKey = true;
            Rfb::Client client(listener);
            client.Connect(TunnelOptions(login, server.GetPort()));
            CHECK(server.AcceptClient(15000));
            server.HandshakeNone("RFB 003.008\n", 8, 6, "tunnelled");
            std::vector<int32_t> encodings = server.ReadClientSetup();
            CHECK(!encodings.empty());
            CHECK(listener.WaitFor([&]() { return listener.connected; }));
            CHECK(listener.sshHostKeyQuestions == 1);
            CHECK(listener.lastSshHostKey.fingerprint.rfind("SHA256:", 0) == 0);
            CHECK(listener.lastSshHostKey.previous.empty());
            CHECK(listener.lastSshHostKey.host == login.host);
            fingerprint = listener.lastSshHostKey.fingerprint;
            CHECK(client.IsEncrypted());
            CHECK(client.GetEncryptionName() == "SSH");

            // Both directions: an update from the server reaches the listener, and the client's request for the
            // next one reaches the server, all inside the tunnel.
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            update.U16(0).U16(0).U16(1).U16(1).S32(Rfb::Encoding::Raw);
            uint32_t pixel = 0x00112233;
            update.Append(&pixel, 4);
            server.Send(update);
            CHECK(listener.WaitFor([&]() { return listener.updates == 1; }));
            CHECK(server.In().U8() == Rfb::ClientMessage::FramebufferUpdateRequest);
            CHECK(server.In().U8() == 1);
            server.In().Skip(8);
            client.SendKeyEvent('a', true);
            CHECK(server.In().U8() == Rfb::ClientMessage::KeyEvent);
            server.In().Skip(7);

            client.Disconnect();
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            CHECK(listener.disconnectError.empty());
        });

        RunTest("A trusted host key isn't asked about; a changed one is, with the old fingerprint", [&]() {
            {
                FakeServer server;
                RecordingListener listener;
                Rfb::Client client(listener);
                Rfb::ConnectionOptions options = TunnelOptions(login, server.GetPort());
                options.trustedSshHostKey = fingerprint;
                client.Connect(options);
                CHECK(server.AcceptClient(15000));
                server.HandshakeNone("RFB 003.008\n", 8, 6, "tunnelled");
                server.ReadClientSetup();
                CHECK(listener.WaitFor([&]() { return listener.connected; }));
                CHECK(listener.sshHostKeyQuestions == 0);
                client.Disconnect();
            }
            {
                FakeServer server;
                RecordingListener listener;
                Rfb::Client client(listener);
                Rfb::ConnectionOptions options = TunnelOptions(login, server.GetPort());
                options.trustedSshHostKey = "SHA256:not-the-same-key-at-all";
                client.Connect(options); // the question is refused (trustSshHostKey is false)
                CHECK(listener.WaitFor([&]() { return listener.disconnected; }, 15000));
                CHECK(listener.sshHostKeyQuestions == 1);
                CHECK(listener.lastSshHostKey.previous == "SHA256:not-the-same-key-at-all");
                CHECK(!listener.connected);
                CHECK(listener.disconnectError.empty()); // the user's decision, not a fault
                CHECK(!listener.authenticationFailed);
            }
        });

        RunTest("A rejected SSH password counts as an authentication failure, and a missing one is asked for", [&]() {
            {
                FakeServer server;
                RecordingListener listener;
                Rfb::Client client(listener);
                Rfb::ConnectionOptions options = TunnelOptions(login, server.GetPort());
                options.trustedSshHostKey = fingerprint;
                options.credentials.password = "not " + login.password;
                client.Connect(options);
                CHECK(listener.WaitFor([&]() { return listener.disconnected; }, 15000));
                CHECK(listener.authenticationFailed);
                CHECK(listener.disconnectError.find("SSH") != std::string::npos);
            }
            {
                FakeServer server;
                RecordingListener listener;
                listener.credentialsToGive.username = login.user;
                listener.credentialsToGive.password = login.password;
                Rfb::Client client(listener);
                Rfb::ConnectionOptions options = TunnelOptions(login, server.GetPort());
                options.trustedSshHostKey = fingerprint;
                options.credentials = Rfb::Credentials();
                client.Connect(options);
                CHECK(server.AcceptClient(15000));
                server.HandshakeNone("RFB 003.008\n", 8, 6, "tunnelled");
                server.ReadClientSetup();
                CHECK(listener.WaitFor([&]() { return listener.connected; }));
                CHECK(listener.credentialRequests == 1);
                CHECK(listener.lastKind == Rfb::CredentialsKind::SshAccount);
                client.Disconnect();
            }
        });

        RunTest("A tunnel to a port with no VNC server fails with a reason", [&]() {
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options = TunnelOptions(login, 1);
            options.trustedSshHostKey = fingerprint;
            client.Connect(options);
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }, 15000));
            CHECK(!listener.connected);
            CHECK(listener.disconnectError.find("VNC server") != std::string::npos);
        });
    }
} // namespace MarconiTest
