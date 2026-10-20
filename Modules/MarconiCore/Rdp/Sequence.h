// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's connection-sequence PDUs after MCS: the Client Info packet (credentials and options), the capability
// exchange (the server's Demand Active and our Confirm Active), and the finalisation PDUs (synchronize, control,
// font). MS-RDPBCGR 2.2.1.11, 2.2.1.13, 2.2.1.14-19. RDP milestone 3.

#pragma once

#include <MarconiCore/Rdp/Pdu.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        struct LogonInfo
        {
            std::string domain;
            std::string user;
            std::string password;
            std::string shell;       // program to run instead of the desktop; usually empty
            std::string workingDir;  // its working folder; usually empty
            bool autoLogon = true;   // use the password without the server asking again
        };

        // The Client Info PDU (TS_INFO_PACKET inside a security header), the whole thing ready to go in a Send Data
        // Request. Strings are sent UTF-16LE.
        Bytes BuildClientInfo(const LogonInfo& logon);

        // Parses a Demand Active PDU: the share id the server assigned, and (for the log) its capability-set count.
        bool ParseDemandActive(const Bytes& pdu, uint32_t& shareId, uint16_t& capabilitySetCount);

        // The Confirm Active PDU answering a Demand Active: our capability sets for `shareId`, from `source` (the
        // user channel). `desktopWidth`/`Height` go in the Bitmap capability set.
        Bytes BuildConfirmActive(uint32_t shareId, uint16_t source, uint16_t desktopWidth, uint16_t desktopHeight);

        // The finalisation PDUs (each a Share Data PDU), in the order the client sends them.
        Bytes BuildSynchronize(uint32_t shareId, uint16_t source, uint16_t targetUser);
        Bytes BuildControlCooperate(uint32_t shareId, uint16_t source);
        Bytes BuildControlRequest(uint32_t shareId, uint16_t source);
        Bytes BuildFontList(uint32_t shareId, uint16_t source);
    } // namespace Rdp
} // namespace Marconi
