// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for Socket's connect (racing several addresses so one that never answers doesn't stall the connection), for
// a send to a slow receiver giving way to Shutdown, and for the send queue

#include "TestSupport.h"

#include <MarconiCore/Net/SendQueue.h>
#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Net/SocketPlatform.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        constexpr const char* kBlackHole = "10.255.255.1"; // non-routable: a connect hangs until it times out

        long long MillisecondsSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                .count();
        }
    } // namespace

    void RunSocketTests()
    {
        RunTest("Connect doesn't wait for an address that never answers", []() {
            // e.g. a host whose IPv6 address is listed first but firewalled, as with a Windows VNC server.
            Net::Socket listener;
            std::string error;
            CHECK(listener.Listen("127.0.0.1", 0, error));
            Net::Socket client;
            auto start = std::chrono::steady_clock::now();
            CHECK(client.ConnectAddresses({kBlackHole, "127.0.0.1"}, listener.GetLocalPort(), 10000, error));
            CHECK(client.IsOpen());
            CHECK(MillisecondsSince(start) < 2000);
        });

        RunTest("A connected socket has keep-alive on, so a dead link is noticed", []() {
            Net::Socket listener;
            std::string error;
            CHECK(listener.Listen("127.0.0.1", 0, error));
            Net::Socket client;
            CHECK(client.ConnectAddresses({"127.0.0.1"}, listener.GetLocalPort(), 10000, error));
            int on = 0;
            socklen_t length = sizeof(on);
#if defined(_WIN32)
            SOCKET handle = static_cast<SOCKET>(client.GetHandle());
#else
            int handle = static_cast<int>(client.GetHandle());
#endif
            CHECK(getsockopt(handle, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&on), &length) == 0);
            CHECK(on != 0);
        });

        RunTest("Connect uses the first address when it answers", []() {
            Net::Socket listener;
            std::string error;
            CHECK(listener.Listen("127.0.0.1", 0, error));
            Net::Socket client;
            auto start = std::chrono::steady_clock::now();
            CHECK(client.ConnectAddresses({"127.0.0.1", kBlackHole}, listener.GetLocalPort(), 10000, error));
            CHECK(MillisecondsSince(start) < 1000);
        });

        RunTest("Connect reports a refused connection promptly", []() {
            uint16_t port = 0;
            {
                Net::Socket unused;
                std::string error;
                CHECK(unused.Listen("127.0.0.1", 0, error));
                port = unused.GetLocalPort();
            }
            Net::Socket client;
            std::string error;
            auto start = std::chrono::steady_clock::now();
            CHECK(!client.ConnectAddresses({"127.0.0.1"}, port, 10000, error));
            CHECK(!error.empty() && !client.IsOpen());
            CHECK(MillisecondsSince(start) < 3000);
        });

        RunTest("Connect gives up on addresses that never answer after the timeout", []() {
            Net::Socket client;
            std::string error;
            auto start = std::chrono::steady_clock::now();
            CHECK(!client.ConnectAddresses({kBlackHole, "10.255.255.2"}, 5900, 1000, error));
            long long elapsed = MillisecondsSince(start);
            CHECK(elapsed >= 900 && elapsed < 2500);
            CHECK(error.find("Timed out") != std::string::npos);
        });

        RunTest("Connect stops promptly when cancelled", []() {
            std::atomic<bool> cancel{true};
            Net::Socket client;
            std::string error;
            auto start = std::chrono::steady_clock::now();
            CHECK(!client.ConnectAddresses({kBlackHole}, 5900, 10000, error, &cancel));
            CHECK(MillisecondsSince(start) < 500);
            CHECK(error == "Cancelled");
        });

        RunTest("Connect by name stops promptly when cancelled", []() {
            std::atomic<bool> cancel{true};
            Net::Socket client;
            std::string error;
            auto start = std::chrono::steady_clock::now();
            CHECK(!client.Connect("localhost", 5900, 10000, error, &cancel));
            CHECK(MillisecondsSince(start) < 500);
            CHECK(error == "Cancelled");
        });

        RunTest("A send to a receiver that has stopped reading gives up when the socket is shut down", []() {
            Net::Socket listener;
            std::string error;
            CHECK(listener.Listen("127.0.0.1", 0, error));
            Net::Socket client;
            CHECK(client.ConnectAddresses({"127.0.0.1"}, listener.GetLocalPort(), 10000, error));
            Net::Socket server;
            CHECK(listener.Accept(server, 5000, error)); // and never reads
            std::atomic<bool> finished{false};
            bool sent = true;
            std::thread sender([&]() {
                std::vector<uint8_t> data(64 * 1024 * 1024);
                sent = client.SendAll(data.data(), data.size());
                finished = true;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(!finished); // stuck: the receiver's buffers are full
            auto start = std::chrono::steady_clock::now();
            client.Shutdown();
            sender.join();
            CHECK(MillisecondsSince(start) < 1000);
            CHECK(!sent);
        });

        RunTest("The send queue runs jobs in order, and a waiting pointer move gives way to the next", []() {
            Net::SendQueue queue;
            std::mutex mutex;
            std::vector<int> ran;
            auto record = [&](int value) {
                return [&, value]() {
                    std::lock_guard<std::mutex> lock(mutex);
                    ran.push_back(value);
                };
            };
            queue.Post(record(0)); // ignored: not started
            queue.Start();
            std::mutex gate;
            gate.lock();
            queue.Post([&]() { std::lock_guard<std::mutex> hold(gate); }); // holds the queue while the rest wait
            queue.Post(record(1));
            queue.Post(record(2), 7);
            queue.Post(record(3), 7); // replaces 2
            queue.Post(record(4));
            queue.Post(record(5), 7); // 4 is in between, so this doesn't replace 3
            gate.unlock();
            CHECK(queue.WaitUntilSent(5000));
            {
                std::lock_guard<std::mutex> lock(mutex);
                CHECK((ran == std::vector<int>{1, 3, 4, 5}));
            }
            queue.Stop();
            queue.Post(record(6)); // ignored: stopped
            queue.Stop();          // twice is fine
            std::lock_guard<std::mutex> lock(mutex);
            CHECK(ran.size() == 4);
        });

        RunTest("Stopping the send queue drops what's waiting", []() {
            Net::SendQueue queue;
            queue.Start();
            std::mutex gate;
            gate.lock();
            std::atomic<int> ran{0};
            std::atomic<bool> started{false};
            queue.Post([&]() {
                started = true;
                std::lock_guard<std::mutex> hold(gate);
                ++ran;
            });
            for (int i = 0; i < 10; ++i)
            {
                queue.Post([&]() { ++ran; });
            }
            while (!started)
            {
                std::this_thread::yield();
            }
            std::thread unlocker([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                gate.unlock();
            });
            queue.Stop(); // waits for the job in progress only
            unlocker.join();
            CHECK(ran == 1);
        });
    }
} // namespace MarconiTest
