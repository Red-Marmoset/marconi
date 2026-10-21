// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Timing pictures end to end: the server's timestamps, the measuring test pattern, and the real client timing
// updates over a link with a known delay

#include "DelayProxy.h"
#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        // Reads whole messages off an in-memory transport.
        class Viewer
        {
        public:
            explicit Viewer(Wire::Transport& transport) : m_reader(transport), m_transport(transport) {}
            void Send(const Wire::Bytes& framed) { m_transport.Write(framed.data(), framed.size()); }
            Wire::Type Next(Wire::Bytes& bytesOut, Wire::Reader& readerOut)
            {
                bytesOut = m_reader.Next();
                readerOut = Wire::Reader(bytesOut);
                return Wire::ReadType(readerOut);
            }

        private:
            Wire::MessageReader m_reader;
            Wire::Transport& m_transport;
        };

        bool Contains(const std::vector<Wire::Rect>& rects, const Wire::Rect& wanted)
        {
            for (const Wire::Rect& rect : rects)
            {
                if (rect.x == wanted.x && rect.y == wanted.y && rect.width == wanted.width &&
                    rect.height == wanted.height)
                {
                    return true;
                }
            }
            return false;
        }

        // Runs a session for a viewer that offers `capabilities`, through login, and returns what Welcome granted,
        // the first FrameEnd and the Pong to a Ping sent afterwards.
        void RunTimedSession(uint64_t capabilities, uint64_t& granted, Wire::FrameEnd& firstEnd, Wire::Pong& pong)
        {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);
            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            Server::Settings settings;
            Server::Session session(serverTransport, screen, input, settings);
            session.SetFrameIntervalMs(1);
            std::thread serverThread([&]() { session.Run(); });

            Viewer viewer(clientTransport);
            Wire::Bytes bytes;
            Wire::Reader body(nullptr, 0);
            Wire::Hello hello;
            hello.capabilities = capabilities;
            viewer.Send(hello.Encode());
            CHECK(viewer.Next(bytes, body) == Wire::Type::Welcome);
            granted = Wire::Welcome::Decode(body).capabilities;
            viewer.Send(Wire::Login{""}.Encode());
            CHECK(viewer.Next(bytes, body) == Wire::Type::LoginResult);

            bool gotEnd = false;
            for (int i = 0; i < 1000 && !gotEnd; ++i)
            {
                if (viewer.Next(bytes, body) == Wire::Type::FrameEnd)
                {
                    firstEnd = Wire::FrameEnd::Decode(body);
                    gotEnd = true;
                }
            }
            CHECK(gotEnd);

            Wire::Ping ping;
            ping.token = 77;
            viewer.Send(ping.Encode());
            bool gotPong = false;
            for (int i = 0; i < 100000 && !gotPong; ++i)
            {
                if (viewer.Next(bytes, body) == Wire::Type::Pong)
                {
                    pong = Wire::Pong::Decode(body);
                    gotPong = true;
                }
            }
            CHECK(gotPong);

            session.Stop();
            clientToServer.Close();
            serverToClient.Close();
            serverThread.join();
        }
    } // namespace

    void RunPerformanceTests()
    {
        RunTest("A viewer that asks for timestamps gets the server's times on frames and pongs", []() {
            const uint64_t start = Wire::NowMicros();
            uint64_t granted = 0;
            Wire::FrameEnd end;
            Wire::Pong pong;
            RunTimedSession(Wire::Capability::MultipleDisplays | Wire::Capability::Timestamps, granted, end, pong);
            const uint64_t finish = Wire::NowMicros();

            CHECK((granted & Wire::Capability::Timestamps) != 0);
            CHECK(end.hasTimes);
            // Same computer, same clock: captured, then sent, both during the test.
            CHECK(end.captureMicros >= start && end.captureMicros <= end.sentMicros && end.sentMicros <= finish);
            CHECK(end.compressMicros <= end.sentMicros - end.captureMicros);
            CHECK(pong.token == 77);
            CHECK(pong.hasTimes);
            CHECK(pong.receivedMicros >= start && pong.receivedMicros <= pong.answeredMicros &&
                  pong.answeredMicros <= finish);
        });

        RunTest("A viewer that doesn't ask for timestamps gets none", []() {
            uint64_t granted = 0;
            Wire::FrameEnd end;
            Wire::Pong pong;
            RunTimedSession(Wire::Capability::MultipleDisplays, granted, end, pong);
            CHECK((granted & Wire::Capability::Timestamps) == 0);
            CHECK(!end.hasTimes);
            CHECK(pong.token == 77);
            CHECK(!pong.hasTimes);
        });

        RunTest("The test pattern draws its counters and the pointer, and reports where they changed", []() {
            Server::TestPatternScreen small(4, 3);
            CHECK(small.PanelRect().width == 0); // too small to carry the digits: unchanged from before

            Server::TestPatternScreen screen(400, 200);
            const Wire::Rect panel = screen.PanelRect();
            CHECK(panel.width == Server::TestPatternScreen::kPanelWidth);
            CHECK(panel.height == Server::TestPatternScreen::kPanelHeight);

            Server::Capture first;
            CHECK(screen.Grab(first));
            CHECK(first.dirty.size() == 1 && first.dirty[0].width == 400 && first.dirty[0].height == 200);
            // The panel's background is black where no segment is lit (its top-left corner).
            CHECK(first.pixels[static_cast<size_t>(panel.y) * 400 + panel.x] == 0u);

            screen.SetPointer(300, 150, 0x01);
            Server::Capture second;
            CHECK(screen.Grab(second));
            CHECK(Contains(second.dirty, panel));
            CHECK(Contains(second.dirty, Wire::Rect{295, 145, 11, 11}));
            // The cross is red while a button is held.
            CHECK(second.pixels[150u * 400u + 300u] == 0x00FF0000u);
            // The frame counter moved from 0 to 1, so the panel's pixels differ between the two grabs.
            bool panelChanged = false;
            for (int32_t y = panel.y; y < panel.y + panel.height && !panelChanged; ++y)
            {
                for (int32_t x = panel.x; x < panel.x + panel.width; ++x)
                {
                    size_t i = static_cast<size_t>(y) * 400 + x;
                    if (first.pixels[i] != second.pixels[i])
                    {
                        panelChanged = true;
                        break;
                    }
                }
            }
            CHECK(panelChanged);

            // Moving the pointer reports both where the cross was and where it is now; releasing turns it white.
            screen.SetPointer(310, 150, 0);
            Server::Capture third;
            CHECK(screen.Grab(third));
            CHECK(Contains(third.dirty, Wire::Rect{295, 145, 11, 11}));
            CHECK(Contains(third.dirty, Wire::Rect{305, 145, 11, 11}));
            CHECK(third.pixels[150u * 400u + 310u] == 0x00FFFFFFu);

            // A pointer that doesn't move adds nothing.
            Server::Capture fourth;
            CHECK(screen.Grab(fourth));
            CHECK(!Contains(fourth.dirty, Wire::Rect{305, 145, 11, 11}));
        });

        RunTest("Over a link with 25 ms each way, the client measures the round trip and each update's age", []() {
            Server::TestPatternScreen screen(64, 48);
            RecordingInput input;
            Server::Settings settings;
            settings.port = 0;
            settings.serverName = "Slow Link PC";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(16);
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(5000); });

            DelayProxy proxy(25);
            CHECK(proxy.Start(listener.GetPort(), error));

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = proxy.GetPort();
            options.clientName = "slow viewer";
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }, 10000));

            // The client pings by itself once pictures flow; the answer crosses the link twice.
            CHECK(clientListener.WaitFor([&]() { return client.GetHealth().roundTripMs >= 0; }, 10000));
            const int roundTrip = client.GetHealth().roundTripMs;
            CHECK(roundTrip >= 50);
            CHECK(roundTrip < 1000);

            // Once the clocks are compared, updates carry the server's times: each is at least one link-delay old
            // on arrival. The clocks are the same here, so the comparison must come out close to zero difference.
            PerformanceSnapshot snapshot;
            CHECK(clientListener.WaitFor(
                [&]() { return client.GetPerformance(1000 * 1000, snapshot) && snapshot.pipelineMs >= 0; }, 10000));
            CHECK(snapshot.frames > 0);
            CHECK(snapshot.networkMs >= 20);
            CHECK(snapshot.networkMs < 250);
            CHECK(snapshot.pipelineMs >= 20);
            CHECK(snapshot.kilobitsPerSecond > 0);

            // A paint marks what was received as shown.
            client.NotePainted();
            CHECK(client.GetPerformance(1000 * 1000, snapshot));
            CHECK(snapshot.showMs >= 0);

            client.Disconnect();
            proxy.Stop();
            listener.Stop();
            serverThread.join();
        });
    }
} // namespace MarconiServerTest
