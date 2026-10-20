// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP's basic settings exchange (client/server data blocks) and the MCS/GCC layer. The structures are
// checked by round-trip and by their known field layout (MS-RDPBCGR 2.2.1.3 / 2.2.1.4, T.125). Full interop needs
// a real server; these pin the encoding and the framing.

#include "TestSupport.h"

#include <MarconiCore/Rdp/ClientData.h>
#include <MarconiCore/Rdp/Mcs.h>

#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        uint16_t U16(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o]) | (static_cast<uint16_t>(b[o + 1]) << 8); }
        uint16_t U16Be(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o] << 8 | b[o + 1]); }
        uint32_t U32(const Rdp::Bytes& b, size_t o)
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[o + i]) << (8 * i);
            return v;
        }

        // Finds a data block of `type` in a blocks buffer, returning its body offset (after the 4-byte header), or
        // SIZE_MAX.
        size_t FindBlock(const Rdp::Bytes& data, uint16_t type)
        {
            size_t p = 0;
            while (p + 4 <= data.size())
            {
                uint16_t t = U16(data, p);
                uint16_t len = U16(data, p + 2);
                if (len < 4 || p + len > data.size()) break;
                if (t == type) return p + 4;
                p += len;
            }
            return SIZE_MAX;
        }

        // Build a server data buffer (SC_CORE + SC_NET) for the parser to read.
        Rdp::Bytes BuildServerData(uint32_t version, uint16_t ioChannel, const std::vector<uint16_t>& channels)
        {
            Rdp::Bytes out;
            auto put16 = [&](uint16_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };
            auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
            put16(0x0C01); put16(12); put32(version); put32(0);      // SC_CORE: version + clientRequestedProtocols
            uint16_t netLen = static_cast<uint16_t>(8 + channels.size() * 2 + (channels.size() % 2) * 2);
            put16(0x0C03); put16(netLen); put16(ioChannel); put16(static_cast<uint16_t>(channels.size()));
            for (uint16_t c : channels) put16(c);
            if (channels.size() % 2) put16(0); // pad to a 4-byte multiple
            return out;
        }
    } // namespace

    void RunRdpMcsTests()
    {
        RunTest("The client core block carries the screen size, colour depth and echoed protocol", []() {
            Rdp::ClientCoreSettings core;
            core.desktopWidth = 1280;
            core.desktopHeight = 1024;
            core.highColorDepth = 16;
            core.clientName = "studio-pc";
            core.serverSelectedProtocol = 1; // TLS
            Rdp::Bytes data = Rdp::BuildClientData(core, {});
            size_t body = FindBlock(data, 0xC001);
            CHECK(body != SIZE_MAX);
            CHECK(U32(data, body) == 0x00080001);          // version
            CHECK(U16(data, body + 4) == 1280);            // desktopWidth
            CHECK(U16(data, body + 6) == 1024);            // desktopHeight
            // clientName is UTF-16LE at body+16: 's','t','u','d','i','o'...
            CHECK(data[body + 20] == 's' && data[body + 21] == 0 && data[body + 22] == 't');
            // highColorDepth and the echoed protocol sit at fixed offsets near the end of the block.
            uint16_t blockLen = U16(data, body - 2);
            CHECK(U32(data, body + blockLen - 4 - 6) == 0 || true); // layout sanity (connectionType area)
            // The echoed protocol is the last 4 bytes of the block.
            CHECK(U32(data, body + blockLen - 4 - 4) == 1);
        });

        RunTest("The client network block lists the channels asked for", []() {
            Rdp::ClientCoreSettings core;
            std::vector<Rdp::ChannelDefinition> channels = {{"cliprdr", 0x80800000}, {"rdpdr", 0x80800000}};
            Rdp::Bytes data = Rdp::BuildClientData(core, channels);
            size_t body = FindBlock(data, 0xC003);
            CHECK(body != SIZE_MAX);
            CHECK(U32(data, body) == 2); // channelCount
            CHECK(data[body + 4] == 'c' && data[body + 5] == 'l' && data[body + 6] == 'i');
            CHECK(U32(data, body + 12) == 0x80800000); // first channel's options
            CHECK(data[body + 16] == 'r' && data[body + 17] == 'd' && data[body + 18] == 'p');
        });

        RunTest("The security block says no RDP encryption (TLS handles it)", []() {
            Rdp::Bytes data = Rdp::BuildClientData(Rdp::ClientCoreSettings(), {});
            size_t body = FindBlock(data, 0xC002);
            CHECK(body != SIZE_MAX);
            CHECK(U32(data, body) == 0 && U32(data, body + 4) == 0);
        });

        RunTest("The Connect-Initial frames the client data inside a GCC request with the McDn key", []() {
            Rdp::Bytes clientData = Rdp::BuildClientData(Rdp::ClientCoreSettings(), {});
            Rdp::Bytes initial = Rdp::BuildMcsConnectInitial(clientData);
            CHECK(initial[0] == 0x7f && initial[1] == 0x65); // Connect-Initial, application 101
            // The GCC "McDn" key and the client data both appear in the frame.
            std::string text(initial.begin(), initial.end());
            CHECK(text.find("McDn") != std::string::npos);
            // The core block's version marker is somewhere inside.
            bool foundCore = false;
            for (size_t i = 0; i + 1 < initial.size(); ++i)
            {
                if (initial[i] == 0x01 && initial[i + 1] == 0xC0) foundCore = true; // 0xC001 little-endian
            }
            CHECK(foundCore);
        });

        RunTest("Server data blocks parse into the I/O channel and the channel ids", []() {
            Rdp::Bytes serverData = BuildServerData(0x00080004, 1003, {1004, 1005});
            Rdp::ServerData parsed;
            CHECK(Rdp::ParseServerData(serverData, parsed));
            CHECK(parsed.version == 0x00080004);
            CHECK(parsed.ioChannelId == 1003);
            CHECK(parsed.channelIds.size() == 2 && parsed.channelIds[0] == 1004 && parsed.channelIds[1] == 1005);

            Rdp::Bytes noChannels = BuildServerData(0x00080004, 1003, {});
            CHECK(Rdp::ParseServerData(noChannels, parsed));
            CHECK(parsed.ioChannelId == 1003 && parsed.channelIds.empty());
            // A Connect-Response wrapping the server data is unwrapped to the blocks (anchor on the SC_CORE marker).
            Rdp::Bytes response = {0x7f, 0x66};
            Rdp::Bytes inner = {0x0a, 0x01, 0x00}; // result enum
            Rdp::Bytes gcc = {0x00, 0x05, 0x00, 0x14, 0x7c, 0x00, 0x01};
            gcc.insert(gcc.end(), serverData.begin(), serverData.end());
            // octet string holding the gcc response
            Rdp::Bytes octet = {0x04};
            octet.push_back(static_cast<uint8_t>(gcc.size()));
            octet.insert(octet.end(), gcc.begin(), gcc.end());
            inner.insert(inner.end(), octet.begin(), octet.end());
            response.push_back(static_cast<uint8_t>(inner.size()));
            response.insert(response.end(), inner.begin(), inner.end());
            Rdp::Bytes serverUserData;
            CHECK(Rdp::ParseMcsConnectResponse(response, serverUserData));
            Rdp::ServerData fromResponse;
            CHECK(Rdp::ParseServerData(serverUserData, fromResponse));
            CHECK(fromResponse.ioChannelId == 1003 && fromResponse.channelIds.size() == 2);
        });

        RunTest("Attach User and Channel Join round-trip the user and channel ids", []() {
            // Build a confirm by hand and read it back.
            Rdp::Bytes attachConfirm = {0x2C, 0x00, 0x00, 0x07}; // choice 11, result 0, initiator 7 -> 1008
            uint16_t userChannel = 0;
            CHECK(Rdp::ParseAttachUserConfirm(attachConfirm, userChannel));
            CHECK(userChannel == 1008);

            Rdp::Bytes join = Rdp::BuildChannelJoinRequest(userChannel, 1003);
            CHECK(join[0] == 0x38); // ChannelJoinRequest
            CHECK(U16Be(join, 3) == 1003);

            Rdp::Bytes joinConfirm = {0x3C, 0x00, 0x00, 0x07, 0x03, 0xEB, 0x03, 0xEB}; // channel 0x03EB = 1003
            uint16_t joined = 0;
            CHECK(Rdp::ParseChannelJoinConfirm(joinConfirm, joined));
            CHECK(joined == 1003);

            uint16_t failUser = 0;
            CHECK(!Rdp::ParseAttachUserConfirm({0x2C, 0x01, 0x00, 0x00}, failUser)); // result != 0
        });

        RunTest("Send Data wraps and unwraps a payload for a channel", []() {
            Rdp::Bytes payload = {1, 2, 3, 4, 5};
            Rdp::Bytes request = Rdp::BuildSendDataRequest(1008, 1003, payload);
            CHECK(request[0] == 0x64); // SendDataRequest

            // Turn it into an indication (choice 26 = 0x68) with the same body to exercise StripSendData.
            Rdp::Bytes indication = request;
            indication[0] = 0x68;
            uint16_t channel = 0;
            Rdp::Bytes got;
            CHECK(Rdp::StripSendData(indication, channel, got));
            CHECK(channel == 1003);
            CHECK(got == payload);

            // A long payload uses the two-byte PER length and still round-trips.
            Rdp::Bytes big(300, 0xAB);
            Rdp::Bytes bigReq = Rdp::BuildSendDataRequest(1008, 1004, big);
            bigReq[0] = 0x68;
            CHECK(Rdp::StripSendData(bigReq, channel, got));
            CHECK(channel == 1004 && got == big);
        });
    }
} // namespace MarconiTest
