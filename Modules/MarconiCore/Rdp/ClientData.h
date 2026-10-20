// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The client data blocks exchanged in RDP's basic settings (MS-RDPBCGR 2.2.1.3): Core, Security and Network, which
// the client sends inside the GCC Conference Create Request, and the matching server blocks it reads back from the
// Conference Create Response. RDP milestone 3 in Documentation/RDP.md.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        struct ClientCoreSettings
        {
            uint16_t desktopWidth = 1024;
            uint16_t desktopHeight = 768;
            uint16_t highColorDepth = 16;      // bits per pixel asked for (8, 15, 16, 24)
            uint32_t keyboardLayout = 0x409;   // US; the OS layout id
            std::string clientName;            // this computer's name, trimmed to 15 characters
            uint32_t serverSelectedProtocol = 0; // the protocol the negotiation chose, echoed back (required)
        };

        struct ChannelDefinition
        {
            std::string name;      // up to 7 ASCII characters, e.g. "cliprdr"
            uint32_t options = 0;  // CHANNEL_OPTION_* flags
        };

        // TS_UD_CS_CORE, TS_UD_CS_SEC and TS_UD_CS_NET concatenated: the user data for the Conference Create
        // Request. `channels` may be empty.
        Bytes BuildClientData(const ClientCoreSettings& core, const std::vector<ChannelDefinition>& channels);

        // What the client needs from the server's data blocks (TS_UD_SC_CORE and TS_UD_SC_NET).
        struct ServerData
        {
            uint32_t version = 0;
            uint16_t ioChannelId = 0;              // the MCS channel the RDP data rides on
            std::vector<uint16_t> channelIds;      // the virtual channels, in the order the client asked for them
        };

        // Parses the server's data blocks (the user data from the Conference Create Response). False if malformed.
        bool ParseServerData(const Bytes& userData, ServerData& out);
    } // namespace Rdp
} // namespace Marconi
