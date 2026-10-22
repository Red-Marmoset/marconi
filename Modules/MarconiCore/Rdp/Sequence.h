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

        // What the server's Demand Active says about its desktop, from its capability sets.
        struct DemandActiveInfo
        {
            uint32_t shareId = 0;
            uint16_t capabilitySetCount = 0;
            uint16_t desktopWidth = 0;         // from the Bitmap capability set; 0 if it sent none
            uint16_t desktopHeight = 0;
            uint16_t preferredBitsPerPixel = 0; // the depth the server will send
            bool fastPathOutput = false;        // the General capability set allows fast-path output
        };
        bool ParseDemandActiveInfo(const Bytes& pdu, DemandActiveInfo& out);

        // The Confirm Active PDU answering a Demand Active: our capability sets for `shareId`, from `source` (the
        // user channel). `desktopWidth`/`Height` go in the Bitmap capability set.
        Bytes BuildConfirmActive(uint32_t shareId, uint16_t source, uint16_t desktopWidth, uint16_t desktopHeight);

        // The finalisation PDUs (each a Share Data PDU), in the order the client sends them.
        Bytes BuildSynchronize(uint32_t shareId, uint16_t source, uint16_t targetUser);
        Bytes BuildControlCooperate(uint32_t shareId, uint16_t source);
        Bytes BuildControlRequest(uint32_t shareId, uint16_t source);
        Bytes BuildFontList(uint32_t shareId, uint16_t source);

        // A Refresh Rect PDU asking for the whole desktop (`width` by `height`) again (MS-RDPBCGR 2.2.11.2).
        Bytes BuildRefreshRect(uint32_t shareId, uint16_t source, uint16_t width, uint16_t height);

        // A Set Error Info PDU's body (MS-RDPBCGR 2.2.5.1.1): why the server is about to end the connection.
        bool ParseSetErrorInfo(const Bytes& body, uint32_t& errorInfo);
        // Plain words for an error info code.
        std::string ErrorInfoText(uint32_t errorInfo);
    } // namespace Rdp
} // namespace Marconi
