// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP milestone 1: the X.224 / TPKT security negotiation (build, parse, and the exchange over a link).
// An opt-in end-to-end test (MARCONI_RDP_TEST=host:port) negotiates and upgrades to TLS against a real server.

#include "TestSupport.h"

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rdp/Negotiation.h>
#include <MarconiCore/Rdp/Security.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // A security link backed by in-memory buffers: what the client sends lands in `sent`, and `toDeliver` is
        // handed back to its reads, a chunk at a time, so short reads are exercised.
        struct FakeLink
        {
            Rdp::Bytes sent;
            std::deque<Rdp::Bytes> toDeliver;

            Rdp::SecurityLink Link()
            {
                Rdp::SecurityLink link;
                link.sendAll = [this](const void* data, size_t length) {
                    const uint8_t* p = static_cast<const uint8_t*>(data);
                    sent.insert(sent.end(), p, p + length);
                    return true;
                };
                link.receive = [this](void* buffer, size_t length) -> ptrdiff_t {
                    if (toDeliver.empty())
                    {
                        return 0; // end of stream
                    }
                    Rdp::Bytes& chunk = toDeliver.front();
                    size_t n = std::min(length, chunk.size());
                    std::memcpy(buffer, chunk.data(), n);
                    chunk.erase(chunk.begin(), chunk.begin() + n);
                    if (chunk.empty())
                    {
                        toDeliver.pop_front();
                    }
                    return static_cast<ptrdiff_t>(n);
                };
                return link;
            }
        };

        // A TPKT + X.224 Connection Confirm carrying an RDP_NEG_RSP or _FAILURE (type 2 or 3).
        Rdp::Bytes Confirm(uint8_t type, uint32_t value)
        {
            Rdp::Bytes neg = {type, 0x00, 0x08, 0x00, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
            Rdp::Bytes x224;
            x224.push_back(static_cast<uint8_t>(6 + neg.size())); // LI
            x224.push_back(0xD0);                                 // CC
            for (int i = 0; i < 5; ++i)
            {
                x224.push_back(0x00); // dst-ref, src-ref, class
            }
            x224.insert(x224.end(), neg.begin(), neg.end());
            size_t total = 4 + x224.size();
            Rdp::Bytes packet = {0x03, 0x00, static_cast<uint8_t>(total >> 8), static_cast<uint8_t>(total & 0xFF)};
            packet.insert(packet.end(), x224.begin(), x224.end());
            return packet;
        }
    } // namespace

    void RunRdpTests()
    {
        RunTest("The RDP connection request asks for the chosen protocols in a well-formed TPKT/X.224 packet", []() {
            Rdp::Bytes request = Rdp::BuildConnectionRequest(Rdp::kTls | Rdp::kCredSsp);
            CHECK(request.size() == 19); // TPKT(4) + X224 header(7) + RDP_NEG_REQ(8)
            CHECK(request[0] == 0x03 && request[1] == 0x00);
            CHECK((static_cast<size_t>(request[2]) << 8 | request[3]) == request.size());
            CHECK(request[4] == request.size() - 5); // X.224 length indicator counts the bytes after itself
            CHECK(request[5] == 0xE0);               // Connection Request
            CHECK(request[11] == 0x01);              // RDP_NEG_REQ type
            CHECK(request[13] == 0x08 && request[14] == 0x00); // its length, 8
            CHECK(request[15] == (Rdp::kTls | Rdp::kCredSsp) && request[16] == 0x00); // requested protocols, LE
        });

        RunTest("A routing cookie goes in the request before the negotiation structure", []() {
            Rdp::Bytes request = Rdp::BuildConnectionRequest(Rdp::kTls, "alice");
            std::string text(request.begin(), request.end());
            CHECK(text.find("Cookie: mstshash=alice\r\n") != std::string::npos);
            CHECK(request[0] == 0x03);
            CHECK((static_cast<size_t>(request[2]) << 8 | request[3]) == request.size());
        });

        RunTest("A negotiation response reports the protocol the server chose", []() {
            Rdp::NegotiationResponse response;
            std::string error;
            CHECK(Rdp::ParseConnectionConfirm(Confirm(0x02, Rdp::kTls), response, error));
            CHECK(response.hasNegotiation && !response.failed && response.selectedProtocol == Rdp::kTls);
            CHECK(Rdp::ParseConnectionConfirm(Confirm(0x02, Rdp::kCredSsp), response, error));
            CHECK(response.selectedProtocol == Rdp::kCredSsp);
        });

        RunTest("A negotiation failure is reported with its code and a readable reason", []() {
            Rdp::NegotiationResponse response;
            std::string error;
            CHECK(Rdp::ParseConnectionConfirm(Confirm(0x03, 0x00000005), response, error));
            CHECK(response.hasNegotiation && response.failed && response.failureCode == 0x00000005);
            CHECK(Rdp::NegotiationFailureText(5).find("Network Level Authentication") != std::string::npos);
        });

        RunTest("A bare Connection Confirm (no negotiation) is success meaning standard security", []() {
            Rdp::Bytes bare = {0x03, 0x00, 0x00, 0x0B, 0x06, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00};
            Rdp::NegotiationResponse response;
            std::string error;
            CHECK(Rdp::ParseConnectionConfirm(bare, response, error));
            CHECK(!response.hasNegotiation && !response.failed);
            CHECK(response.selectedProtocol == Rdp::kStandardRdp);
        });

        RunTest("A reply that isn't a Connection Confirm is rejected with a reason", []() {
            Rdp::NegotiationResponse response;
            std::string error;
            Rdp::Bytes notConfirm = {0x03, 0x00, 0x00, 0x0B, 0x06, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00};
            CHECK(!Rdp::ParseConnectionConfirm(notConfirm, response, error) && !error.empty());
            Rdp::Bytes shortHeader = {0x03, 0x00, 0x00, 0x03};
            CHECK(!Rdp::ParseConnectionConfirm(shortHeader, response, error) && !error.empty());
        });

        RunTest("The negotiation sends the request and reads the confirm over a link, through short reads", []() {
            FakeLink server;
            Rdp::Bytes confirm = Confirm(0x02, Rdp::kCredSsp);
            for (uint8_t byte : confirm)
            {
                server.toDeliver.push_back({byte}); // one byte at a time: ReadFully must reassemble the frame
            }
            Rdp::NegotiationResponse response;
            std::string error;
            CHECK(Rdp::NegotiateSecurity(server.Link(), Rdp::kTls | Rdp::kCredSsp, "", response, error));
            CHECK(response.hasNegotiation && response.selectedProtocol == Rdp::kCredSsp);
            CHECK(server.sent == Rdp::BuildConnectionRequest(Rdp::kTls | Rdp::kCredSsp));
        });

        RunTest("A server that hangs up before answering fails the negotiation with a reason", []() {
            FakeLink server; // nothing to deliver: receive returns 0
            Rdp::NegotiationResponse response;
            std::string error;
            CHECK(!Rdp::NegotiateSecurity(server.Link(), Rdp::kTls, "", response, error));
            CHECK(!error.empty());
        });

        // Opt-in, against a real RDP server: MARCONI_RDP_TEST=host:port (xrdp or Windows). Negotiates TLS or NLA and
        // upgrades to TLS, which every current server requires.
        const char* target = std::getenv("MARCONI_RDP_TEST");
        if (target == nullptr)
        {
            printf("  (set MARCONI_RDP_TEST=host:port, a real RDP server, for the end-to-end TLS test)\n");
            return;
        }
        RunTest("A real RDP server negotiates TLS or NLA, and the TLS handshake then succeeds", [&]() {
            std::string text = target;
            size_t colon = text.rfind(':');
            CHECK(colon != std::string::npos);
            std::string host = text.substr(0, colon);
            uint16_t port = static_cast<uint16_t>(std::atoi(text.c_str() + colon + 1));
            Net::Socket socket;
            std::string error;
            CHECK(socket.Connect(host, port, 5000, error));
            Rdp::SecurityLink link;
            link.receive = [&socket](void* buffer, size_t length) { return socket.Receive(buffer, length); };
            link.sendAll = [&socket](const void* data, size_t length) { return socket.SendAll(data, length); };
            Rdp::NegotiationResponse response;
            CHECK(Rdp::NegotiateSecurity(link, Rdp::kTls | Rdp::kCredSsp, "", response, error));
            CHECK(response.hasNegotiation && !response.failed);
            printf("  %s chose protocol 0x%x\n", target, response.selectedProtocol);
            CHECK((response.selectedProtocol & (Rdp::kTls | Rdp::kCredSsp)) != 0);
            // The chosen layer is TLS either way (CredSSP runs over TLS); upgrade and confirm the handshake.
            Net::TlsOptions options;
            options.hostName = host;
            options.trustDecision = [](const Net::TlsCertificate&) { return true; }; // a test: trust whatever it sends
            std::unique_ptr<Net::TlsStream> tls = Net::CreateTlsStream(socket, options);
            CHECK(tls && tls->Handshake(error));
            if (tls)
            {
                printf("  TLS: %s\n", tls->GetProtocolName().c_str());
                CHECK(tls->GetProtocolName().rfind("TLS", 0) == 0);
            }
        });
    }
} // namespace MarconiTest
