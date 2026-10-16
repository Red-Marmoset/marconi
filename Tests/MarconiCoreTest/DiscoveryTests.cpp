// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for DNS message coding, the UDP socket, and mDNS discovery against a local fake responder

#include "TestSupport.h"

#include <MarconiCore/Net/DnsMessage.h>
#include <MarconiCore/Net/Discovery.h>
#include <MarconiCore/Net/UdpSocket.h>

#include <thread>

namespace MarconiTest
{
    using namespace Marconi;

    void RunDiscoveryTests()
    {
        RunTest("DNS query encoding", []() {
            std::vector<uint8_t> query = Net::Dns::EncodeQuery(0x1234, {{"_rfb._tcp.local", Net::Dns::kTypePtr}});
            CHECK(query == HexBytes("1234 0000 0001 0000 0000 0000"
                                    "045f726662 045f746370 056c6f63616c 00 000c 0001"));
        });

        RunTest("DNS response round trip with PTR, SRV and A", []() {
            Net::Dns::Record ptr;
            ptr.name = "_rfb._tcp.local";
            ptr.type = Net::Dns::kTypePtr;
            ptr.ttl = 4500;
            ptr.target = "Ford._rfb._tcp.local";
            Net::Dns::Record srv;
            srv.name = "Ford._rfb._tcp.local";
            srv.type = Net::Dns::kTypeSrv;
            srv.target = "Mac.local";
            srv.port = 5900;
            Net::Dns::Record a;
            a.name = "Mac.local";
            a.type = Net::Dns::kTypeA;
            a.ipv4 = "192.168.1.20";
            std::vector<uint8_t> bytes = Net::Dns::EncodeResponse(0, {ptr, srv, a});
            std::vector<Net::Dns::Record> records;
            CHECK(Net::Dns::ParseMessage(bytes.data(), bytes.size(), records));
            CHECK(records.size() == 3);
            CHECK(records[0].type == Net::Dns::kTypePtr && records[0].target == "Ford._rfb._tcp.local");
            CHECK(records[0].ttl == 4500);
            CHECK(records[1].type == Net::Dns::kTypeSrv && records[1].target == "Mac.local" && records[1].port == 5900);
            CHECK(records[2].type == Net::Dns::kTypeA && records[2].ipv4 == "192.168.1.20");
        });

        RunTest("ParseMessage follows name compression", []() {
            // One PTR answer for _rfb._tcp.local whose target is "Ford" + pointer to offset 12 (the answer's name).
            std::vector<uint8_t> bytes = HexBytes("0000 8400 0000 0001 0000 0000"
                                                  "045f726662 045f746370 056c6f63616c 00" // offset 12
                                                  "000c 0001 00001194 0007"
                                                  "04466f7264 c00c");
            std::vector<Net::Dns::Record> records;
            CHECK(Net::Dns::ParseMessage(bytes.data(), bytes.size(), records));
            CHECK(records.size() == 1 && records[0].target == "Ford._rfb._tcp.local");
        });

        RunTest("ParseMessage rejects pointer loops and truncation", []() {
            std::vector<uint8_t> loop = HexBytes("0000 8400 0000 0001 0000 0000 c00c 000c 0001 00000000 0000");
            std::vector<Net::Dns::Record> records;
            CHECK(!Net::Dns::ParseMessage(loop.data(), loop.size(), records));
            std::vector<uint8_t> truncated = HexBytes("0000 8400 0000 0001 0000 0000 045f726662");
            CHECK(!Net::Dns::ParseMessage(truncated.data(), truncated.size(), records));
            CHECK(!Net::Dns::ParseMessage(nullptr, 0, records));
        });

        RunTest("UDP send and receive on localhost", []() {
            Net::UdpSocket server;
            Net::UdpSocket client;
            std::string error;
            CHECK(server.Bind("127.0.0.1", 0, error));
            CHECK(client.Open(error));
            CHECK(client.SendTo("127.0.0.1", server.GetLocalPort(), "ping", 4));
            char buffer[16] = {};
            std::string from;
            uint16_t fromPort = 0;
            CHECK(server.ReceiveFrom(buffer, sizeof(buffer), 1000, &from, &fromPort) == 4);
            CHECK(std::string(buffer, 4) == "ping" && from == "127.0.0.1" && fromPort == client.GetLocalPort());
            CHECK(client.ReceiveFrom(buffer, sizeof(buffer), 100) == 0); // timeout
        });

        RunTest("AssembleServices merges duplicates", []() {
            auto make = [](const std::string& name, uint16_t type, const std::string& target, uint16_t port,
                           const std::string& ipv4) {
                Net::Dns::Record r;
                r.name = name;
                r.type = type;
                r.target = target;
                r.port = port;
                r.ipv4 = ipv4;
                return r;
            };
            std::vector<Net::Dns::Record> records = {
                make("_rfb._tcp.local", Net::Dns::kTypePtr, "Ford._rfb._tcp.local", 0, ""),
                make("_rfb._tcp.local", Net::Dns::kTypePtr, "Ford._rfb._tcp.local", 0, ""), // duplicate answer
                make("Ford._rfb._tcp.local", Net::Dns::kTypeSrv, "Mac.local", 5900, ""),
                make("Mac.local", Net::Dns::kTypeA, "", 0, "192.168.1.20"),
                make("_ssh._tcp.local", Net::Dns::kTypePtr, "Ford._ssh._tcp.local", 0, ""),
                make("Ford._ssh._tcp.local", Net::Dns::kTypeSrv, "Mac.local", 22, ""),
                make("_http._tcp.local", Net::Dns::kTypePtr, "Printer._http._tcp.local", 0, ""), // not ours
            };
            std::vector<Net::DiscoveredService> services = Net::AssembleServices(records);
            CHECK(services.size() == 2);
            CHECK(services[0].instance == "Ford" && services[0].protocol == Protocol::Vnc && services[0].port == 5900);
            CHECK(services[0].hostName == "Mac.local" && services[0].ipv4 == "192.168.1.20");
            CHECK(services[1].protocol == Protocol::Ssh && services[1].port == 22);
        });

        RunTest("Discover asks for our services and assembles the answers", []() {
            Net::UdpSocket responder;
            std::string error;
            CHECK(responder.Bind("127.0.0.1", 0, error));
            std::thread fake([&responder]() {
                uint8_t buffer[512];
                std::string from;
                uint16_t fromPort = 0;
                ptrdiff_t got = responder.ReceiveFrom(buffer, sizeof(buffer), 2000, &from, &fromPort);
                if (got <= 0)
                {
                    return;
                }
                Net::Dns::Record ptr;
                ptr.name = "_rfb._tcp.local";
                ptr.type = Net::Dns::kTypePtr;
                ptr.target = "Ford._rfb._tcp.local";
                Net::Dns::Record srv;
                srv.name = "Ford._rfb._tcp.local";
                srv.type = Net::Dns::kTypeSrv;
                srv.target = "Mac.local";
                srv.port = 5900;
                Net::Dns::Record a;
                a.name = "Mac.local";
                a.type = Net::Dns::kTypeA;
                a.ipv4 = "192.168.1.20";
                std::vector<uint8_t> reply = Net::Dns::EncodeResponse(0, {ptr, srv, a});
                responder.SendTo(from, fromPort, reply.data(), reply.size());
            });
            std::vector<Net::DiscoveredService> services = Net::Discover(500, "127.0.0.1", responder.GetLocalPort());
            fake.join();
            CHECK(services.size() == 1 && services[0].instance == "Ford" && services[0].ipv4 == "192.168.1.20");
        });

        RunTest("Discover asks again for records a responder left out", []() {
            Net::UdpSocket responder;
            std::string error;
            CHECK(responder.Bind("127.0.0.1", 0, error));
            int queries = 0;
            std::thread fake([&responder, &queries]() {
                uint8_t buffer[512];
                std::string from;
                uint16_t fromPort = 0;
                while (responder.ReceiveFrom(buffer, sizeof(buffer), 2000, &from, &fromPort) > 0)
                {
                    Net::Dns::Record reply;
                    if (++queries == 1)
                    {
                        reply.name = "_rfb._tcp.local"; // only the PTR, as some responders do
                        reply.type = Net::Dns::kTypePtr;
                        reply.target = "Ford._rfb._tcp.local";
                    }
                    else if (queries == 2)
                    {
                        reply.name = "Ford._rfb._tcp.local";
                        reply.type = Net::Dns::kTypeSrv;
                        reply.target = "Mac.local";
                        reply.port = 5900;
                    }
                    else
                    {
                        reply.name = "Mac.local";
                        reply.type = Net::Dns::kTypeA;
                        reply.ipv4 = "192.168.1.20";
                    }
                    std::vector<uint8_t> bytes = Net::Dns::EncodeResponse(0, {reply});
                    responder.SendTo(from, fromPort, bytes.data(), bytes.size());
                    if (queries == 3)
                    {
                        return;
                    }
                }
            });
            std::vector<Net::DiscoveredService> services = Net::Discover(300, "127.0.0.1", responder.GetLocalPort());
            fake.join();
            CHECK(queries == 3);
            CHECK(services.size() == 1 && services[0].port == 5900 && services[0].ipv4 == "192.168.1.20");
        });
    }
} // namespace MarconiTest
