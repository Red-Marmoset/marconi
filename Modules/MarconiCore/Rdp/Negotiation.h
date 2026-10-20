// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's first exchange: the X.224 Connection Request and Confirm (wrapped in TPKT) that agree on which security
// layer the rest of the connection uses. See Documentation/RDP.md.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // The security layers a client may ask for and a server may choose, as a bit set (MS-RDPBCGR 2.2.1.1.1).
        enum SecurityProtocol : uint32_t
        {
            kStandardRdp = 0x00000000, // the legacy RC4 layer (Marconi doesn't implement it; here for completeness)
            kTls = 0x00000001,         // TLS 1.x
            kCredSsp = 0x00000002,     // CredSSP (NLA); implies TLS underneath
            kCredSspWithEarlyUser = 0x00000008,
        };

        // Why a server refused every protocol the client asked for (the RDP_NEG_FAILURE codes, MS-RDPBCGR 2.2.1.2.2).
        std::string NegotiationFailureText(uint32_t failureCode);

        // The TPKT + X.224 Connection Request carrying an RDP_NEG_REQ for `requestedProtocols`. `cookie` is the
        // optional "Cookie: mstshash=..." routing token some servers expect; empty leaves it out.
        Bytes BuildConnectionRequest(uint32_t requestedProtocols, const std::string& cookie = {});

        struct NegotiationResponse
        {
            bool hasNegotiation = false; // false for an old server that sent a bare Connection Confirm (means Standard)
            bool failed = false;         // the server sent an RDP_NEG_FAILURE
            uint32_t selectedProtocol = kStandardRdp; // when hasNegotiation and not failed
            uint32_t failureCode = 0;                 // when failed
        };

        // Parses a TPKT + X.224 Connection Confirm. False (with `error`) if the bytes aren't a well-formed Confirm.
        // A well-formed Confirm with no negotiation structure is success with hasNegotiation == false.
        bool ParseConnectionConfirm(const Bytes& packet, NegotiationResponse& out, std::string& error);

        // The 4-byte TPKT header's total-length field (bytes 2-3, big-endian), or 0 if `header` isn't a TPKT header.
        // Used to read exactly one TPKT frame from a stream: read 4 bytes, then this many minus 4 more.
        size_t TpktLength(const uint8_t header[4]);
    } // namespace Rdp
} // namespace Marconi
