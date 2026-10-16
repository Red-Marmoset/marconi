// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for Net::Probe: identifying VNC, SSH and RDP by their greetings, against local fake services

#include "TestSupport.h"

#include <MarconiCore/Net/Probe.h>
#include <MarconiCore/Net/Socket.h>

#include <chrono>
#include <functional>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // Accepts one connection on 127.0.0.1 and runs `behaviour` on it; records what the client sent.
        class FakeService
        {
        public:
            explicit FakeService(std::function<void(Net::Socket&, std::string&)> behaviour)
            {
                std::string error;
                m_listener.Listen("127.0.0.1", 0, error);
                m_thread = std::thread([this, behaviour]() {
                    Net::Socket client;
                    std::string error;
                    if (m_listener.Accept(client, 5000, error))
                    {
                        behaviour(client, m_received);
                    }
                });
            }
            ~FakeService()
            {
                m_thread.join();
            }
            uint16_t GetPort() const { return m_listener.GetLocalPort(); }

        private:
            Net::Socket m_listener;
            std::thread m_thread;
            std::string m_received;
        };

        void Send(Net::Socket& socket, const std::string& text)
        {
            socket.SendAll(text.data(), text.size());
        }

        // Reads whatever arrives within `ms` into `out`.
        void Drain(Net::Socket& socket, std::string& out, int ms)
        {
            char buffer[256];
            while (socket.WaitReadable(ms))
            {
                ptrdiff_t got = socket.Receive(buffer, sizeof(buffer));
                if (got <= 0)
                {
                    break;
                }
                out.append(buffer, static_cast<size_t>(got));
            }
        }

        // Reads until at least `wanted` bytes arrived or `ms` passed (what an RDP server does with the request).
        void ReadAtLeast(Net::Socket& socket, std::string& out, size_t wanted, int ms)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            char buffer[256];
            while (out.size() < wanted && std::chrono::steady_clock::now() < deadline)
            {
                if (!socket.WaitReadable(50))
                {
                    continue;
                }
                ptrdiff_t got = socket.Receive(buffer, sizeof(buffer));
                if (got <= 0)
                {
                    break;
                }
                out.append(buffer, static_cast<size_t>(got));
            }
        }

        // Connection Confirm carrying an RDP Negotiation Response, and one carrying an RDP Negotiation Failure.
        const std::string kRdpConfirm("\x03\x00\x00\x13\x0e\xd0\x00\x00\x12\x34"
                                      "\x00\x02\x00\x08\x00\x01\x00\x00\x00",
                                      19);
        const std::string kRdpFailure("\x03\x00\x00\x13\x0e\xd0\x00\x00\x12\x34"
                                      "\x00\x03\x00\x08\x00\x05\x00\x00\x00",
                                      19);

        bool IsConfirm(const std::string& data, size_t size)
        {
            return Net::IsRdpConnectionConfirm(reinterpret_cast<const uint8_t*>(data.data()), size);
        }

        long long MillisecondsSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                .count();
        }
    } // namespace

    void RunProbeTests()
    {
        RunTest("IdentifyGreeting and IsRdpConnectionConfirm", []() {
            CHECK(Net::IdentifyGreeting("RFB 003.008\n") == Protocol::Vnc);
            CHECK(Net::IdentifyGreeting("SSH-2.0-OpenSSH_9.6\r\n") == Protocol::Ssh);
            CHECK(Net::IdentifyGreeting("HTTP/1.1 400 Bad Request") == Protocol::Auto);
            CHECK(Net::IdentifyGreeting("") == Protocol::Auto);
            CHECK(IsConfirm(kRdpConfirm, kRdpConfirm.size()));
            CHECK(IsConfirm(kRdpFailure, kRdpFailure.size()));
            CHECK(!IsConfirm("RFB 003.008\n", 12));
            CHECK(!IsConfirm(kRdpConfirm, 5));
        });

        RunTest("DefaultProbeTargets", []() {
            std::vector<Net::ProbeTarget> all = Net::DefaultProbeTargets({"h"});
            CHECK(all.size() == 3);
            CHECK(all[0].port == 5900 && all[0].expected == Protocol::Vnc);
            CHECK(all[1].port == 3389 && all[1].expected == Protocol::Rdp);
            CHECK(all[2].port == 22 && all[2].expected == Protocol::Ssh);
            std::vector<Net::ProbeTarget> one = Net::DefaultProbeTargets({"h", 2222});
            CHECK(one.size() == 1 && one[0].port == 2222 && one[0].expected == Protocol::Auto);
            std::vector<Net::ProbeTarget> forced = Net::DefaultProbeTargets({"h", 0, "", Protocol::Rdp});
            CHECK(forced.size() == 1 && forced[0].port == 3389 && forced[0].expected == Protocol::Rdp);
        });

        RunTest("Probe identifies VNC, SSH and RDP", []() {
            FakeService vnc([](Net::Socket& s, std::string& got) {
                Send(s, "RFB 003.889\n");
                Drain(s, got, 300);
            });
            FakeService ssh([](Net::Socket& s, std::string& got) {
                Send(s, "SSH-2.0-OpenSSH_9.6\r\n");
                Drain(s, got, 300);
            });
            FakeService rdp([](Net::Socket& s, std::string& got) {
                ReadAtLeast(s, got, 19, 1500);
                Send(s, kRdpConfirm);
            });
            std::vector<Net::ProbeTarget> targets = {
                {vnc.GetPort(), Protocol::Vnc}, {ssh.GetPort(), Protocol::Ssh}, {rdp.GetPort(), Protocol::Rdp}};
            std::vector<Net::ProbeResult> results = Net::Probe("127.0.0.1", targets, 1500);
            CHECK(results.size() == 3);
            CHECK(results[0].reachable && results[0].protocol == Protocol::Vnc && results[0].detail == "RFB 003.889");
            CHECK(results[1].reachable && results[1].protocol == Protocol::Ssh &&
                  results[1].detail == "SSH-2.0-OpenSSH_9.6");
            CHECK(results[2].reachable && results[2].protocol == Protocol::Rdp);
        });

        RunTest("Probe on an unknown port waits for a greeting, then tries RDP", []() {
            FakeService rdp([](Net::Socket& s, std::string& got) {
                ReadAtLeast(s, got, 19, 1500); // silent until the client asks, like a real RDP server
                Send(s, kRdpFailure);          // a negotiation failure is still RDP
            });
            std::vector<Net::ProbeResult> results = Net::Probe("127.0.0.1", {{rdp.GetPort(), Protocol::Auto}}, 1500);
            CHECK(results.size() == 1 && results[0].protocol == Protocol::Rdp);
        });

        RunTest("Probe never sends RDP bytes to an expected-VNC port", []() {
            std::string received;
            {
                FakeService slowVnc([&received](Net::Socket& s, std::string& got) {
                    Drain(s, got, 1200); // says nothing, like a busy server
                    received = got;
                });
                std::vector<Net::ProbeResult> results =
                    Net::Probe("127.0.0.1", {{slowVnc.GetPort(), Protocol::Vnc}}, 1000);
                CHECK(results.size() == 1 && results[0].reachable && results[0].protocol == Protocol::Auto);
            }
            CHECK(received.empty());
        });

        RunTest("Probe reports closed ports and unknown greetings", []() {
            uint16_t closedPort = 0;
            {
                Net::Socket unused;
                std::string error;
                unused.Listen("127.0.0.1", 0, error);
                closedPort = unused.GetLocalPort();
            }
            FakeService http([](Net::Socket& s, std::string& got) {
                Send(s, "HTTP/1.1 400 Bad Request\r\n");
                Drain(s, got, 300);
            });
            std::vector<Net::ProbeResult> results =
                Net::Probe("127.0.0.1", {{closedPort, Protocol::Vnc}, {http.GetPort(), Protocol::Ssh}}, 1500);
            CHECK(results.size() == 2);
            CHECK(!results[0].reachable && !results[0].detail.empty());
            CHECK(results[1].reachable && results[1].protocol == Protocol::Auto);
        });

        RunTest("Probe runs targets in parallel", []() {
            FakeService silent1([](Net::Socket& s, std::string& got) { Drain(s, got, 1500); });
            FakeService silent2([](Net::Socket& s, std::string& got) { Drain(s, got, 1500); });
            auto start = std::chrono::steady_clock::now();
            Net::Probe("127.0.0.1", {{silent1.GetPort(), Protocol::Vnc}, {silent2.GetPort(), Protocol::Ssh}}, 800);
            CHECK(MillisecondsSince(start) < 1300);
        });

        RunTest("Probe gives the greeting its full time after a slow connect", []() {
            // A slow lookup or connect (here: a first address that never answers, so the second starts 250 ms
            // later) mustn't eat into the time a server has to send its banner.
            FakeService vnc([](Net::Socket& s, std::string& got) {
                std::this_thread::sleep_for(std::chrono::milliseconds(450));
                Send(s, "RFB 003.008\n");
                Drain(s, got, 300);
            });
            std::vector<Net::ProbeResult> results =
                Net::ProbeAddresses({"10.255.255.1", "127.0.0.1"}, {{vnc.GetPort(), Protocol::Vnc}}, 600);
            CHECK(results.size() == 1 && results[0].protocol == Protocol::Vnc);
        });

        RunTest("Probe stops promptly when cancelled", []() {
            std::atomic<bool> cancel{true};
            auto start = std::chrono::steady_clock::now();
            std::vector<Net::ProbeResult> results = Net::Probe("10.255.255.1", {{5900, Protocol::Vnc}}, 10000, &cancel);
            CHECK(MillisecondsSince(start) < 300);
            CHECK(results.size() == 1 && !results[0].reachable);
        });
    }
} // namespace MarconiTest
