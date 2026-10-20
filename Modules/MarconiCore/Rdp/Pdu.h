// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP PDU framing: the basic security header, the Share Control header (MS-RDPBCGR 2.2.8.1.1.1.1) and the Share
// Data header (2.2.8.1.1.1.2) that the connection-sequence and operation PDUs sit inside, over MCS. RDP milestone 3.

#pragma once

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // Share Control PDU types (the low 4 bits of pduType).
        enum class ShareControlType : uint16_t
        {
            DemandActive = 1,
            ConfirmActive = 3,
            Deactivate = 6,
            Data = 7,
        };

        // Share Data PDU types (pduType2 in the Share Data header).
        enum class ShareDataType : uint8_t
        {
            Update = 2,
            Control = 20,
            Pointer = 27,
            Input = 28,
            Synchronize = 31,
            RefreshRect = 33,
            FontList = 39,
            FontMap = 40,
        };

        // A basic security header carrying just `flags` (no encryption of its own; TLS/NLA handle that). Prepend to
        // PDUs that need one, such as the Client Info PDU (SEC_INFO_PKT) and the finalisation PDUs after it.
        Bytes BuildSecurityHeader(uint16_t flags);

        // Wraps `body` in a Share Control header of `type`, from `source` (the user channel id).
        Bytes WrapShareControl(ShareControlType type, uint16_t source, const Bytes& body);
        // Reads a Share Control header. False if malformed; `type`, `source` and the body range are returned.
        bool ReadShareControl(const Bytes& pdu, ShareControlType& type, uint16_t& source, Bytes& body);

        // Wraps `body` in a Share Data header (inside a Share Control Data PDU) of `type2` for `shareId`.
        Bytes WrapShareData(uint32_t shareId, uint16_t source, ShareDataType type2, const Bytes& body);
        // Reads a Share Data PDU (a Data Share Control PDU and its Share Data header). False if not a data PDU.
        bool ReadShareData(const Bytes& pdu, ShareDataType& type2, Bytes& body);
    } // namespace Rdp
} // namespace Marconi
