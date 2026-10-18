// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's first exchange: the X.224 Connection Request and Confirm (wrapped in TPKT) that agree on the security layer

#include <MarconiCore/Rdp/Negotiation.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // TPKT (RFC 1006): version 3, reserved 0, then the total length including this 4-byte header.
            void PutTpktHeader(Bytes& out, size_t totalLength)
            {
                out.push_back(0x03);
                out.push_back(0x00);
                out.push_back(static_cast<uint8_t>((totalLength >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>(totalLength & 0xFF));
            }

            void PutU32LE(Bytes& out, uint32_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
            }

            uint32_t GetU32LE(const uint8_t* p)
            {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }

            constexpr uint8_t kX224ConnectionRequest = 0xE0; // CR TPDU code (high nibble; low nibble is the CDT, 0)
            constexpr uint8_t kX224ConnectionConfirm = 0xD0; // CC TPDU code
            constexpr uint8_t kTypeNegReq = 0x01;            // RDP_NEG_REQ
            constexpr uint8_t kTypeNegRsp = 0x02;            // RDP_NEG_RSP
            constexpr uint8_t kTypeNegFailure = 0x03;        // RDP_NEG_FAILURE
        } // namespace

        std::string NegotiationFailureText(uint32_t failureCode)
        {
            switch (failureCode)
            {
                case 0x00000001:
                    return "The server requires TLS, which couldn't be negotiated (SSL_REQUIRED_BY_SERVER)";
                case 0x00000002:
                    return "The server isn't configured for the security this computer offered (SSL_NOT_ALLOWED_BY_SERVER)";
                case 0x00000003:
                    return "The server has no certificate for a secure connection (SSL_CERT_NOT_ON_SERVER)";
                case 0x00000004:
                    return "The server rejected the security request (INCONSISTENT_FLAGS)";
                case 0x00000005:
                    return "The server requires Network Level Authentication (HYBRID_REQUIRED_BY_SERVER)";
                case 0x00000006:
                    return "The server's certificate couldn't be used (SSL_WITH_USER_AUTH_REQUIRED_BY_SERVER)";
                default:
                    return "The server refused the connection (code " + std::to_string(failureCode) + ")";
            }
        }

        Bytes BuildConnectionRequest(uint32_t requestedProtocols, const std::string& cookie)
        {
            // X.224 CR variable part: the optional routing cookie line (ending CR LF), then the 8-byte RDP_NEG_REQ.
            Bytes variable;
            if (!cookie.empty())
            {
                std::string line = "Cookie: mstshash=" + cookie + "\r\n";
                variable.insert(variable.end(), line.begin(), line.end());
            }
            variable.push_back(kTypeNegReq);
            variable.push_back(0x00); // flags
            variable.push_back(0x08); // length low (RDP_NEG_REQ is always 8 bytes)
            variable.push_back(0x00); // length high
            PutU32LE(variable, requestedProtocols);

            // X.224 CR TPDU: length indicator (of the rest of the X.224 header + variable), code, and six fixed bytes
            // (dst-ref, src-ref, class), then the variable part.
            Bytes x224;
            x224.push_back(static_cast<uint8_t>(6 + variable.size())); // LI: bytes after this one
            x224.push_back(kX224ConnectionRequest);
            x224.push_back(0x00); // dst-ref
            x224.push_back(0x00);
            x224.push_back(0x00); // src-ref
            x224.push_back(0x00);
            x224.push_back(0x00); // class / options
            x224.insert(x224.end(), variable.begin(), variable.end());

            Bytes packet;
            PutTpktHeader(packet, 4 + x224.size());
            packet.insert(packet.end(), x224.begin(), x224.end());
            return packet;
        }

        size_t TpktLength(const uint8_t header[4])
        {
            if (header[0] != 0x03)
            {
                return 0;
            }
            return (static_cast<size_t>(header[2]) << 8) | header[3];
        }

        bool ParseConnectionConfirm(const Bytes& packet, NegotiationResponse& out, std::string& error)
        {
            out = NegotiationResponse();
            if (packet.size() < 7 || packet[0] != 0x03)
            {
                error = "The server's reply wasn't a valid RDP Connection Confirm";
                return false;
            }
            size_t total = TpktLength(packet.data());
            if (total != packet.size())
            {
                error = "The server's RDP reply had the wrong length";
                return false;
            }
            uint8_t lengthIndicator = packet[4];
            uint8_t code = packet[5];
            if (code != kX224ConnectionConfirm)
            {
                error = "The server didn't confirm the RDP connection";
                return false;
            }
            // The X.224 header after the LI byte is 6 bytes (code + dst-ref + src-ref + class); anything after that,
            // within the length the LI gave, is the optional negotiation structure.
            size_t variableStart = 5 + 6; // TPKT(4) + LI(1) then 6 header bytes
            size_t variableEnd = 5 + lengthIndicator; // LI counts the bytes after itself
            if (variableEnd > packet.size())
            {
                error = "The server's RDP reply was truncated";
                return false;
            }
            if (variableEnd - variableStart < 8)
            {
                return true; // a bare Confirm: the server speaks only Standard RDP security
            }
            const uint8_t* neg = packet.data() + variableStart;
            uint8_t type = neg[0];
            uint32_t value = GetU32LE(neg + 4);
            if (type == kTypeNegRsp)
            {
                out.hasNegotiation = true;
                out.selectedProtocol = value;
                return true;
            }
            if (type == kTypeNegFailure)
            {
                out.hasNegotiation = true;
                out.failed = true;
                out.failureCode = value;
                return true;
            }
            error = "The server's RDP reply had an unexpected negotiation type";
            return false;
        }
    } // namespace Rdp
} // namespace Marconi
