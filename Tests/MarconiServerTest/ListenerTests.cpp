// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server over a real loopback socket: certificate pinning. A viewer that already knows the server's fingerprint
// connects without being asked about it; a viewer that pins the wrong one, and declines the question it falls back
// to, never gets in.

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        // Turns a lowercase-hex fingerprint (what the listener shows) back into the raw bytes the client pins.
        std::vector<uint8_t> FromHex(const std::string& hex)
        {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i + 1 < hex.size(); i += 2)
            {
                bytes.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
            }
            return bytes;
        }
    } // namespace

    void RunListenerTests()
    {
        RunTest("A second server can't start on a port another server is already listening on", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.port = 0; // a free port
            Server::Listener first(screen, input, settings);
            std::string error;
            CHECK(first.Start(error, "127.0.0.1"));

            Server::Settings same = settings;
            same.port = first.GetPort();
            Server::Listener second(screen, input, same);
            CHECK(!second.Start(error, "127.0.0.1"));
            CHECK(!error.empty());
            first.Stop();
        });

        RunTest("A viewer that pins the server's fingerprint connects without being asked about it", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 0; // a free port
            settings.serverName = "Socket PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);

            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            uint16_t port = listener.GetPort();
            CHECK(port != 0);

            // Start() generated the self-signed identity, so the fingerprint is known before anyone connects.
            std::string fingerprint = listener.GetCertificateFingerprint();
            CHECK(!fingerprint.empty());

            std::thread serverThread([&]() { listener.ServeOne(5000); });

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = port;
            options.clientName = "socket viewer";
            options.trustedCertificateSha256 = FromHex(fingerprint); // pin what the listener presents
            client.Connect(options);

            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(client.IsConnected());
            CHECK(client.IsEncrypted());
            // The pin matched, so the client trusted the certificate itself and never asked the listener.
            CHECK(clientListener.certificateQuestions == 0);

            // A full frame still follows, proving the handshake completed end to end.
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));

            client.Disconnect();
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            listener.Stop();
            serverThread.join();
        });

        RunTest("A connection that says nothing doesn't keep a viewer out, and is dropped", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            settings.port = 0;
            settings.serverName = "Socket PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);
            listener.SetLoginTimeoutMs(1500);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.Run(); });

            // Someone connects and never says a word: before, that held the server for everyone.
            Net::Socket silent;
            CHECK(silent.Connect("127.0.0.1", listener.GetPort(), 2000, error));
            uint8_t marker[4] = {};
            silent.SetReceiveTimeout(2000);
            CHECK(silent.Receive(marker, sizeof(marker)) > 0); // the server has taken the connection on

            // A real viewer still gets in straight away.
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "socket viewer";
            options.trustedCertificateSha256 = FromHex(listener.GetCertificateFingerprint());
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));

            // The silent one is closed once its time to log in runs out.
            silent.SetReceiveTimeout(5000);
            uint8_t more[16];
            CHECK(silent.Receive(more, sizeof(more)) <= 0);
            silent.Close();

            client.Disconnect();
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            listener.Stop();
            serverThread.join();
        });

        RunTest("A second viewer waits its turn while the first is connected", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            settings.port = 0;
            settings.serverName = "Socket PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.Run(); });

            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.trustedCertificateSha256 = FromHex(listener.GetCertificateFingerprint());

            RecordingClientListener firstListener;
            Wire::Client first(firstListener);
            options.clientName = "first";
            first.Connect(options);
            CHECK(firstListener.WaitFor([&]() { return firstListener.connected; }));

            RecordingClientListener secondListener;
            Wire::Client second(secondListener);
            options.clientName = "second";
            second.Connect(options);
            CHECK(!secondListener.WaitFor([&]() { return secondListener.connected; }, 700)); // still waiting

            Server::ViewerSnapshot viewer;
            CHECK(listener.GetViewer(viewer) && viewer.name == "first");
            first.Disconnect();
            CHECK(secondListener.WaitFor([&]() { return secondListener.connected; }));
            CHECK(listener.GetViewer(viewer) && viewer.name == "second");

            second.Disconnect();
            CHECK(secondListener.WaitFor([&]() { return secondListener.disconnected; }));
            listener.Stop();
            serverThread.join();
        });

        RunTest("A viewer that pins the wrong fingerprint and declines the question is turned away", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 0;
            settings.serverName = "Socket PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);

            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            uint16_t port = listener.GetPort();

            std::thread serverThread([&]() { listener.ServeOne(5000); });

            RecordingClientListener clientListener;
            clientListener.trustCertificate = false; // refuse when asked about an unknown certificate
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = port;
            options.clientName = "socket viewer";
            options.trustedCertificateSha256 = std::vector<uint8_t>(32, 0x00); // a fingerprint the server won't match
            client.Connect(options);

            // The pin didn't match, so the client asked; the answer was no, so the handshake fails and the client
            // never reports a connection.
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            CHECK(clientListener.certificateQuestions == 1);
            CHECK(!clientListener.connected);
            CHECK(!client.IsConnected());

            client.Disconnect();
            listener.Stop();
            serverThread.join();
        });
    }
} // namespace MarconiServerTest
