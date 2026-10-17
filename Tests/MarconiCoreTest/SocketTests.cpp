// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for Socket's connect: racing several addresses so one that never answers doesn't stall the connection

#include "TestSupport.h"

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Net/SocketPlatform.h>

#include <chrono>

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
    }
} // namespace MarconiTest
