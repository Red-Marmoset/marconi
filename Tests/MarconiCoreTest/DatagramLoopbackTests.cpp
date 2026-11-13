// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the datagram channel over real UDP sockets on the loopback address: the server and client sessions with
// their worker threads, a fake payload producer, a blocked path that falls back to the control channel, outsiders
// sending junk, and clean shutdown

#include "DatagramFakeLink.h"
#include "TestSupport.h"

#include <MarconiCore/Net/UdpSocket.h>
#include <MarconiCore/Wire/DatagramSession.h>

#include <atomic>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;
    using Wire::Bytes;
    using Wire::LinkState;
    using Wire::SendResult;

    namespace
    {
        constexpr uint8_t kVideo = 1;
        constexpr uint8_t kState = 2;
        constexpr uint16_t kFirstPort = 19250;
        constexpr uint16_t kLastPort = 19259;
        constexpr uint16_t kUnusedPort = 19299;

        Bytes Pattern(uint32_t id, size_t size)
        {
            Bytes bytes(size);
            for (size_t i = 0; i < size; ++i)
            {
                bytes[i] = static_cast<uint8_t>((id * 31 + i * 7 + (i >> 8)) & 0xFF);
            }
            return bytes;
        }

        size_t VideoSize(uint32_t id)
        {
            return id % 10 == 0 ? 80000 : 200 + (id * 613) % 6000;
        }

        bool WaitFor(const std::function<bool()>& condition, int timeoutMs)
        {
            auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < end)
            {
                if (condition())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return condition();
        }

        Wire::DatagramConfig LoopbackConfig()
        {
            Wire::DatagramConfig config = DatagramPair::DefaultConfig();
            config.keepAliveUs = 100000;
            config.reportIntervalUs = 200000;
            config.probeTimeoutUs = 600000;
            config.helloIntervalUs = 100000;
            config.receive.deadlineUs = 250000;
            return config;
        }

        // Both ends in one process, joined by a control "connection" that calls straight into the other session.
        struct LoopbackPair
        {
            std::unique_ptr<Wire::DatagramSession> server;
            std::unique_ptr<Wire::DatagramSession> client;
            std::mutex mutex;
            std::vector<Wire::Delivery> clientGot;
            std::vector<Wire::Delivery> serverGot;
            std::vector<Wire::Gap> clientGaps;
            std::atomic<Wire::DatagramSession*> toServer{nullptr};
            std::atomic<Wire::DatagramSession*> toClient{nullptr};

            // Runs the setup the way the control channel does: offer, accept, start. `offerPort` replaces the port
            // in the offer when non-zero (to point the client at nothing).
            bool Setup(const Wire::DatagramConfig& config, uint16_t offerPort = 0)
            {
                std::string error;
                server = Wire::DatagramSession::CreateServer(
                    config, "127.0.0.1", kFirstPort, kLastPort,
                    [this](const Bytes& framed) { Deliver(toClient, framed); }, error);
                if (!server)
                {
                    std::printf("  server setup failed: %s\n", error.c_str());
                    return false;
                }
                // The offer travels as a framed control message and is decoded on the other side.
                Bytes framed = server->GetOffer().Encode();
                Wire::Reader outer(framed);
                uint64_t length = outer.Varint();
                Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
                CHECK(Wire::ReadType(body) == Wire::Type::DatagramOffer);
                Wire::DatagramOffer offer = Wire::DatagramOffer::Decode(body);
                if (offerPort != 0)
                {
                    offer.port = offerPort;
                }
                client = Wire::DatagramSession::CreateClient(
                    offer, "127.0.0.1", config, [this](const Bytes& framed2) { Deliver(toServer, framed2); }, error);
                if (!client)
                {
                    std::printf("  client setup failed: %s\n", error.c_str());
                    return false;
                }
                toServer = server.get();
                toClient = client.get();
                Wire::DatagramChannel::Handler clientHandler;
                clientHandler.onMessage = [this](const Wire::Delivery& d)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    clientGot.push_back(d);
                };
                clientHandler.onGap = [this](const Wire::Gap& g)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    clientGaps.push_back(g);
                };
                client->Channel().SetHandler(clientHandler);
                Wire::DatagramChannel::Handler serverHandler;
                serverHandler.onMessage = [this](const Wire::Delivery& d)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    serverGot.push_back(d);
                };
                server->Channel().SetHandler(serverHandler);
                server->Start();
                client->Start();
                return true;
            }

            static void Deliver(std::atomic<Wire::DatagramSession*>& to, const Bytes& framed)
            {
                Wire::DatagramSession* session = to.load();
                if (session == nullptr)
                {
                    return;
                }
                Wire::Reader outer(framed);
                uint64_t length = outer.Varint();
                Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
                Wire::Type type = Wire::ReadType(body);
                session->OnControlMessage(type, body);
            }

            void Stop()
            {
                toServer = nullptr;
                toClient = nullptr;
                if (client)
                {
                    client->Stop();
                }
                if (server)
                {
                    server->Stop();
                }
            }

            size_t ClientCount()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return clientGot.size();
            }
            size_t ServerCount()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return serverGot.size();
            }
        };
    } // namespace

    void RunDatagramLoopbackTests()
    {
        RunTest("Datagram loopback: video one way and state the other over real UDP, with a producer thread", []() {
            LoopbackPair pair;
            CHECK(pair.Setup(LoopbackConfig()));
            if (!pair.server || !pair.client)
            {
                return;
            }
            CHECK(pair.server->GetOffer().port >= kFirstPort && pair.server->GetOffer().port <= kLastPort);
            CHECK(WaitFor([&]() { return pair.client->Channel().GetState() == LinkState::Active &&
                                         pair.server->Channel().GetState() == LinkState::Active; },
                          3000));
            const uint32_t videoCount = 200;
            const uint32_t stateCount = 300;
            std::thread videoProducer([&]() {
                for (uint32_t id = 0; id < videoCount; ++id)
                {
                    pair.server->Channel().Send(kVideo, Pattern(id, VideoSize(id)), pair.server->Now());
                    std::this_thread::sleep_for(std::chrono::milliseconds(id % 10 == 0 ? 8 : 2));
                }
            });
            std::thread stateProducer([&]() {
                for (uint32_t id = 0; id < stateCount; ++id)
                {
                    pair.client->Channel().Send(kState, Pattern(id, 24), pair.client->Now());
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
            videoProducer.join();
            stateProducer.join();
            CHECK(WaitFor([&]() { return pair.ClientCount() + [&]() {
                                      std::lock_guard<std::mutex> lock(pair.mutex);
                                      size_t gaps = 0;
                                      for (const auto& g : pair.clientGaps) { gaps += g.count; }
                                      return gaps; }() >= videoCount; },
                          3000));
            size_t gapTotal = 0;
            {
                std::lock_guard<std::mutex> lock(pair.mutex);
                uint32_t last = 0;
                bool first = true;
                for (const Wire::Delivery& d : pair.clientGot)
                {
                    CHECK(d.channel == kVideo && !d.overTcp);
                    CHECK(first || d.messageId > last);
                    CHECK(d.payload == Pattern(d.messageId, VideoSize(d.messageId)));
                    first = false;
                    last = d.messageId;
                }
                for (const auto& g : pair.clientGaps)
                {
                    gapTotal += g.count;
                }
                // Latest-wins: ids only go up, contents intact.
                for (size_t i = 0; i < pair.serverGot.size(); ++i)
                {
                    CHECK(pair.serverGot[i].payload == Pattern(pair.serverGot[i].messageId, 24));
                    CHECK(i == 0 || pair.serverGot[i].messageId > pair.serverGot[i - 1].messageId);
                }
            }
            std::printf("  loopback: %zu of %u video messages delivered, %zu reported lost; %zu of %u state messages\n",
                        pair.ClientCount(), videoCount, gapTotal, pair.ServerCount(), stateCount);
            // Loopback should lose next to nothing; a few drops from the socket buffer are tolerated.
            CHECK(pair.ClientCount() >= videoCount * 9 / 10);
            CHECK(pair.ClientCount() + gapTotal == videoCount);
            CHECK(pair.ServerCount() >= stateCount * 9 / 10);
            // The keep-alives and reports have run: a round trip is measured and the peer reports on us.
            CHECK(WaitFor([&]() { return pair.server->Channel().GetStats().havePeerReport; }, 2000));
            Wire::DatagramStats stats = pair.server->Channel().GetStats();
            CHECK(stats.haveRtt);
            CHECK(stats.rttUs < 50000);
            CHECK(stats.authFailures == 0);
            CHECK(Wire::DatagramStatusText(pair.client->Channel().GetStats()).find("UDP") == 0);
            pair.Stop();
        });

        RunTest("Datagram loopback: a blocked path falls back to the control channel and says so", []() {
            LoopbackPair pair;
            // The client sends its Hello to a port nobody listens on.
            CHECK(pair.Setup(LoopbackConfig(), kUnusedPort));
            if (!pair.server || !pair.client)
            {
                return;
            }
            CHECK(WaitFor([&]() { return pair.client->Channel().GetState() == LinkState::FellBack; }, 3000));
            CHECK(Wire::DatagramStatusText(pair.client->Channel().GetStats()) == "over TCP");
            CHECK(WaitFor([&]() { return pair.server->Channel().GetState() == LinkState::FellBack; }, 1000));
            CHECK(pair.server->Channel().Send(kVideo, Pattern(0, 50000), pair.server->Now()) == SendResult::Tcp);
            CHECK(pair.client->Channel().Send(kState, Pattern(0, 24), pair.client->Now()) == SendResult::Tcp);
            CHECK(WaitFor([&]() { return pair.ClientCount() == 1 && pair.ServerCount() == 1; }, 1000));
            {
                std::lock_guard<std::mutex> lock(pair.mutex);
                CHECK(pair.clientGot[0].overTcp && pair.clientGot[0].payload == Pattern(0, 50000));
                CHECK(pair.serverGot[0].overTcp);
            }
            pair.Stop();
        });

        RunTest("Datagram loopback: outsiders' junk and forged datagrams are ignored and do not steal the path", []() {
            LoopbackPair pair;
            CHECK(pair.Setup(LoopbackConfig()));
            if (!pair.server || !pair.client)
            {
                return;
            }
            CHECK(WaitFor([&]() { return pair.server->Channel().GetState() == LinkState::Active; }, 3000));
            Net::UdpSocket outsider;
            std::string error;
            CHECK(outsider.Open(error));
            DatagramRandom random(5);
            uint16_t port = pair.server->GetOffer().port;
            uint32_t session = pair.server->GetOffer().sessionId;
            for (int i = 0; i < 500; ++i)
            {
                Bytes junk = random.Bytes(20 + random.Below(300));
                if (i & 1)
                {
                    junk[0] = 1;
                    std::memcpy(&junk[1], &session, 4); // the right session id, no key
                }
                outsider.SendTo("127.0.0.1", port, junk.data(), junk.size());
                if (i % 20 == 19)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2)); // don't overrun the socket buffer
                }
            }
            CHECK(WaitFor([&]() { return pair.server->Channel().GetStats().authFailures >= 150; }, 3000));
            // The server still answers the real client, not the outsider.
            size_t before = pair.ClientCount();
            for (uint32_t id = 0; id < 20; ++id)
            {
                pair.server->Channel().Send(kVideo, Pattern(id, 500), pair.server->Now());
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            CHECK(WaitFor([&]() { return pair.ClientCount() >= before + 18; }, 2000));
            CHECK(pair.server->Channel().GetState() == LinkState::Active);
            pair.Stop();
        });

        RunTest("Datagram loopback: sessions start and stop cleanly, quickly and repeatedly", []() {
            for (int round = 0; round < 15; ++round)
            {
                LoopbackPair pair;
                if (!pair.Setup(LoopbackConfig()))
                {
                    CHECK(false);
                    return;
                }
                if (round % 2 == 0)
                {
                    WaitFor([&]() { return pair.client->Channel().GetState() == LinkState::Active; }, 3000);
                }
                std::atomic<bool> sending{true};
                std::thread producer([&]() {
                    while (sending)
                    {
                        pair.server->Channel().Send(kVideo, Pattern(1, 3000), pair.server->Now());
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                auto start = std::chrono::steady_clock::now();
                pair.Stop();
                auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                    start)
                                  .count();
                CHECK(stopMs < 500);
                sending = false;
                producer.join();
                // the destructors run at the end of the scope, with the threads already stopped
            }
        });
    }
} // namespace MarconiTest
