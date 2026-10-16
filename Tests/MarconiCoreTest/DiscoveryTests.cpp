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

    namespace
    {
        // A response header with these section counts.
        Rfb::OutBuffer DnsHeader(uint16_t questions, uint16_t answers, uint16_t authorities = 0)
        {
            Rfb::OutBuffer out;
            out.U16(0).U16(0x8400).U16(questions).U16(answers).U16(authorities).U16(0);
            return out;
        }

        // A name of labels with these lengths, as sent (no compression).
        void AppendName(Rfb::OutBuffer& out, const std::vector<int>& labelLengths)
        {
            for (int length : labelLengths)
            {
                out.U8(static_cast<uint8_t>(length)).Append(std::string(static_cast<size_t>(length), 'a'));
            }
            out.U8(0);
        }

        // An A record for 10.0.0.1 named with these labels, declaring `dataLength` bytes of data.
        void AppendARecord(Rfb::OutBuffer& out, const std::vector<int>& labelLengths, uint16_t dataLength = 4)
        {
            AppendName(out, labelLengths);
            out.U16(Net::Dns::kTypeA).U16(1).U32(120).U16(dataLength);
            out.U8(10).U8(0).U8(0).U8(1);
        }

        bool Parses(const Rfb::OutBuffer& message)
        {
            std::vector<Net::Dns::Record> records;
            return Net::Dns::ParseMessage(message.Data().data(), message.Data().size(), records);
        }
    } // namespace

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

        RunTest("ParseMessage rejects labels longer than 63 bytes", []() {
            Rfb::OutBuffer ok = DnsHeader(0, 1);
            AppendARecord(ok, {63, 5});
            CHECK(Parses(ok));
            Rfb::OutBuffer tooLong = DnsHeader(0, 1);
            AppendARecord(tooLong, {64, 5});
            CHECK(!Parses(tooLong));
        });

        RunTest("ParseMessage rejects names longer than 255 characters", []() {
            Rfb::OutBuffer ok = DnsHeader(0, 1);
            AppendARecord(ok, {63, 63, 63, 60}); // 252 characters with the dots
            CHECK(Parses(ok));
            Rfb::OutBuffer tooLong = DnsHeader(0, 1);
            AppendARecord(tooLong, {63, 63, 63, 63, 63});
            CHECK(!Parses(tooLong));
            // The same limit holds for a name assembled through a compression pointer.
            Rfb::OutBuffer viaPointer = DnsHeader(0, 2);
            AppendARecord(viaPointer, {63, 63, 63}); // name at offset 12
            AppendName(viaPointer, {63, 63});
            std::vector<uint8_t> bytes = viaPointer.Data();
            bytes.pop_back(); // end the second name with a pointer to the first instead
            bytes.push_back(0xC0);
            bytes.push_back(12);
            Rfb::OutBuffer rest;
            rest.U16(Net::Dns::kTypeA).U16(1).U32(120).U16(4).U8(10).U8(0).U8(0).U8(2);
            bytes.insert(bytes.end(), rest.Data().begin(), rest.Data().end());
            std::vector<Net::Dns::Record> records;
            CHECK(!Net::Dns::ParseMessage(bytes.data(), bytes.size(), records));
        });

        RunTest("ParseMessage rejects record data running past the end", []() {
            Rfb::OutBuffer ok = DnsHeader(0, 1);
            AppendARecord(ok, {4}, 4);
            CHECK(Parses(ok));
            Rfb::OutBuffer overrun = DnsHeader(0, 1);
            AppendARecord(overrun, {4}, 5);
            CHECK(!Parses(overrun));
            Rfb::OutBuffer farOverrun = DnsHeader(0, 1);
            AppendARecord(farOverrun, {4}, 0xFFFF);
            CHECK(!Parses(farOverrun));
        });

        RunTest("ParseMessage rejects header counts larger than the message", []() {
            Rfb::OutBuffer answers = DnsHeader(0, 2);
            AppendARecord(answers, {4});
            CHECK(!Parses(answers));
            Rfb::OutBuffer questions = DnsHeader(0xFFFF, 0);
            CHECK(!Parses(questions));
            Rfb::OutBuffer authorities = DnsHeader(0, 1, 1);
            AppendARecord(authorities, {4});
            CHECK(!Parses(authorities));
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
