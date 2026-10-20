// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP PDU framing: the basic security header and the Share Control / Share Data headers.

#include <MarconiCore/Rdp/Pdu.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            }
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }
            uint16_t GetU16(const Bytes& data, size_t offset)
            {
                return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            }

            constexpr uint16_t kProtocolVersion = 0x0010; // the high bits of pduType
        } // namespace

        Bytes BuildSecurityHeader(uint16_t flags)
        {
            Bytes out;
            PutU16(out, flags);
            PutU16(out, 0); // flagsHi
            return out;
        }

        Bytes WrapShareControl(ShareControlType type, uint16_t source, const Bytes& body)
        {
            Bytes out;
            uint16_t totalLength = static_cast<uint16_t>(body.size() + 6);
            PutU16(out, totalLength);
            PutU16(out, static_cast<uint16_t>(static_cast<uint16_t>(type) | kProtocolVersion));
            PutU16(out, source);
            out.insert(out.end(), body.begin(), body.end());
            return out;
        }

        bool ReadShareControl(const Bytes& pdu, ShareControlType& type, uint16_t& source, Bytes& body)
        {
            if (pdu.size() < 6)
            {
                return false;
            }
            uint16_t totalLength = GetU16(pdu, 0);
            if (totalLength < 6 || totalLength > pdu.size())
            {
                return false;
            }
            type = static_cast<ShareControlType>(GetU16(pdu, 2) & 0x000F);
            source = GetU16(pdu, 4);
            body.assign(pdu.begin() + 6, pdu.begin() + totalLength);
            return true;
        }

        Bytes WrapShareData(uint32_t shareId, uint16_t source, ShareDataType type2, const Bytes& body)
        {
            Bytes data;
            PutU32(data, shareId);
            data.push_back(0);                              // pad1
            data.push_back(1);                              // streamId: STREAM_LOW
            PutU16(data, static_cast<uint16_t>(body.size() + 4)); // uncompressedLength: body + this header's tail
            data.push_back(static_cast<uint8_t>(type2));    // pduType2
            data.push_back(0);                              // compressedType: none
            PutU16(data, 0);                                // compressedLength
            data.insert(data.end(), body.begin(), body.end());
            return WrapShareControl(ShareControlType::Data, source, data);
        }

        bool ReadShareData(const Bytes& pdu, ShareDataType& type2, Bytes& body)
        {
            ShareControlType type = ShareControlType::Data;
            uint16_t source = 0;
            Bytes control;
            if (!ReadShareControl(pdu, type, source, control) || type != ShareControlType::Data)
            {
                return false;
            }
            if (control.size() < 12) // the Share Data header is 12 bytes
            {
                return false;
            }
            type2 = static_cast<ShareDataType>(control[8]);
            body.assign(control.begin() + 12, control.end());
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
