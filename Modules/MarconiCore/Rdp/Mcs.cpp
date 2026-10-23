// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MCS (T.125) and the GCC conference wrapper (T.124) for RDP's connection sequence.

#include <MarconiCore/Rdp/Mcs.h>

#include <initializer_list>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // --- BER helpers (Connect-Initial / Response use BER) ---
            void PutBerLength(Bytes& out, size_t length)
            {
                if (length < 0x80)
                {
                    out.push_back(static_cast<uint8_t>(length));
                    return;
                }
                Bytes bytes;
                while (length > 0)
                {
                    bytes.push_back(static_cast<uint8_t>(length & 0xFF));
                    length >>= 8;
                }
                out.push_back(static_cast<uint8_t>(0x80 | bytes.size()));
                for (size_t i = bytes.size(); i > 0; --i)
                {
                    out.push_back(bytes[i - 1]);
                }
            }

            // A BER TLV with a one- or two-byte tag.
            void PutBer(Bytes& out, const Bytes& tag, const Bytes& contents)
            {
                out.insert(out.end(), tag.begin(), tag.end());
                PutBerLength(out, contents.size());
                out.insert(out.end(), contents.begin(), contents.end());
            }
            // An MCS INTEGER as Windows writes it: the fewest big-endian bytes, with no sign byte (65535 is ff ff,
            // as in MS-RDPBCGR 4.1.3).
            Bytes BerInteger(uint32_t value)
            {
                Bytes contents;
                Bytes be;
                uint32_t v = value;
                do
                {
                    be.push_back(static_cast<uint8_t>(v & 0xFF));
                    v >>= 8;
                } while (v > 0);
                for (size_t i = be.size(); i > 0; --i)
                {
                    contents.push_back(be[i - 1]);
                }
                return contents;
            }

            // Reads a BER tag+length at `p`; advances `p` past the length. Returns the content length, or SIZE_MAX
            // on malformed input.
            size_t ReadBerLength(const Bytes& data, size_t& p)
            {
                if (p >= data.size())
                {
                    return SIZE_MAX;
                }
                size_t length = data[p++];
                if (length & 0x80)
                {
                    size_t count = length & 0x7F;
                    if (count == 0 || count > 4 || p + count > data.size())
                    {
                        return SIZE_MAX;
                    }
                    length = 0;
                    for (size_t i = 0; i < count; ++i)
                    {
                        length = (length << 8) | data[p++];
                    }
                }
                return p + length <= data.size() ? length : SIZE_MAX;
            }

            // T.125 DomainParameters: maxChannelIds, maxUserIds, maxTokenIds, numPriorities, minThroughput,
            // maxHeight, maxMCSPDUsize, protocolVersion.
            Bytes DomainParameters(std::initializer_list<uint32_t> values)
            {
                Bytes params;
                for (uint32_t value : values)
                {
                    PutBer(params, {0x02}, BerInteger(value));
                }
                return params;
            }

            // --- PER helpers (the GCC conference wrapper uses aligned PER) ---
            void PutPerLength(Bytes& out, size_t length)
            {
                if (length < 0x80)
                {
                    out.push_back(static_cast<uint8_t>(length));
                }
                else
                {
                    out.push_back(static_cast<uint8_t>(0x80 | ((length >> 8) & 0x3F)));
                    out.push_back(static_cast<uint8_t>(length & 0xFF));
                }
            }

            // The GCC Conference Create Request around the user data (MS-RDPBCGR 2.2.1.3.1). The leading bytes are
            // the fixed T.124 ConnectData / ConnectGCCPDU framing; only the lengths vary.
            Bytes GccConferenceCreateRequest(const Bytes& userData)
        {
            // The body is the ConnectGCCPDU: a fixed PER-encoded ConferenceCreateRequest header, the H.221
            // client-to-server key "McDn", then the user data behind a PER length determinant.
            // The PER-encoded ConferenceCreateRequest header, then the H.221 client-to-server key "Duca", then the
            // user data behind a PER length (MS-RDPBCGR 4.1.1 shows this exact framing).
            Bytes body = {0x00, 0x08, 0x00, 0x10, 0x00, 0x01, 0xc0, 0x00, 0x44, 0x75, 0x63, 0x61};
            PutPerLength(body, userData.size());
            body.insert(body.end(), userData.begin(), userData.end());

            // ConnectData: the T.124 object identifier for Generic Conference Control, then the body behind its own
            // PER length determinant.
            Bytes gcc = {0x00, 0x05, 0x00, 0x14, 0x7c, 0x00, 0x01};
            PutPerLength(gcc, body.size());
            gcc.insert(gcc.end(), body.begin(), body.end());
            return gcc;
        }

        constexpr uint16_t kIoChannelBase = 1001; // MCS channel ids RDP uses start here
        } // namespace

        Bytes FrameX224Data(const Bytes& payload)
        {
            size_t total = payload.size() + 7;
            Bytes out = {0x03, 0x00, static_cast<uint8_t>((total >> 8) & 0xFF), static_cast<uint8_t>(total & 0xFF),
                         0x02, 0xF0, 0x80}; // TPKT header, then X.224 Data (LI 2, code 0xF0, EOT 0x80)
            out.insert(out.end(), payload.begin(), payload.end());
            return out;
        }

        bool UnframeX224Data(const Bytes& frame, Bytes& payload)
        {
            if (frame.size() < 7 || frame[0] != 0x03 || frame[5] != 0xF0)
            {
                return false;
            }
            size_t total = (static_cast<size_t>(frame[2]) << 8) | frame[3];
            if (total != frame.size() || total < 7)
            {
                return false;
            }
            payload.assign(frame.begin() + 7, frame.end());
            return true;
        }

        Bytes BuildMcsConnectInitial(const Bytes& clientData)
        {
            Bytes gcc = GccConferenceCreateRequest(clientData);

            Bytes contents;
            PutBer(contents, {0x04}, {0x01});             // callingDomainSelector
            PutBer(contents, {0x04}, {0x01});             // calledDomainSelector
            PutBer(contents, {0x01}, {0xFF});             // upwardFlag: TRUE
            // The values Windows clients send (MS-RDPBCGR 4.1.3).
            PutBer(contents, {0x30}, DomainParameters({34, 2, 0, 1, 0, 1, 0xFFFF, 2}));                // target
            PutBer(contents, {0x30}, DomainParameters({1, 1, 1, 1, 0, 1, 0x420, 2}));                  // minimum
            PutBer(contents, {0x30}, DomainParameters({0xFFFF, 0xFC17, 0xFFFF, 1, 0, 1, 0xFFFF, 2})); // maximum
            PutBer(contents, {0x04}, gcc);                // userData (the GCC request)

            Bytes out;
            PutBer(out, {0x7f, 0x65}, contents); // Connect-Initial (application 101)
            return out;
        }

        bool ParseMcsConnectResponse(const Bytes& connectResponse, Bytes& serverUserData)
        {
            serverUserData.clear();
            size_t p = 0;
            // Connect-Response is application 102: tag 0x7f 0x66.
            if (connectResponse.size() < 2 || connectResponse[0] != 0x7f || connectResponse[1] != 0x66)
            {
                return false;
            }
            p = 2;
            if (ReadBerLength(connectResponse, p) == SIZE_MAX)
            {
                return false;
            }
            // Inside: result (enum), calledConnectId (int), domainParameters (seq), userData (octet string). Skip
            // each TLV until the final octet string, which holds the GCC Conference Create Response.
            Bytes userData;
            while (p < connectResponse.size())
            {
                if (p + 1 > connectResponse.size())
                {
                    return false;
                }
                uint8_t tag = connectResponse[p++];
                size_t length = ReadBerLength(connectResponse, p);
                if (length == SIZE_MAX)
                {
                    return false;
                }
                if (tag == 0x04) // octet string: the GCC response
                {
                    userData.assign(connectResponse.begin() + p, connectResponse.begin() + p + length);
                }
                p += length;
            }
            if (userData.empty())
            {
                return false;
            }
            // Inside the GCC Conference Create Response, find the user data: skip to the "McDn"/"Duca" key and the
            // PER length, then take the rest. The server data blocks begin right after the user-data length.
            size_t q = 0;
            // The response begins with ConnectData framing; the user data follows a PER length after a fixed part.
            // Find the H.221 server-to-client key "McDn" is not present; servers use a numeric key. Instead, the
            // user data is the tail after the last PER length. Walk to the SC block marker (0x0c 0x01 little-endian
            // type 0x0C01) as a robust anchor.
            for (; q + 4 <= userData.size(); ++q)
            {
                uint16_t type = static_cast<uint16_t>(userData[q]) | (static_cast<uint16_t>(userData[q + 1]) << 8);
                if (type == 0x0C01 || type == 0x0C03 || type == 0x0C02) // SC_CORE / SC_NET / SC_SECURITY
                {
                    serverUserData.assign(userData.begin() + q, userData.end());
                    return true;
                }
            }
            return false;
        }

        Bytes BuildErectDomainRequest()
        {
            // ErectDomainRequest (MCS 0x04 << 2 = 0x04 as the choice byte), subHeight and subInterval both 0.
            return {0x04, 0x01, 0x00, 0x01, 0x00};
        }

        Bytes BuildAttachUserRequest()
        {
            return {0x28}; // AttachUserRequest: choice 10 (0x0A << 2)
        }

        bool ParseAttachUserConfirm(const Bytes& pdu, uint16_t& userChannelId)
        {
            // AttachUserConfirm: choice 11 (0x2C), a result enum, then the initiator (user id) as a 16-bit value
            // biased by 1001.
            if (pdu.size() < 4 || (pdu[0] & 0xFC) != 0x2C)
            {
                return false;
            }
            if (pdu[1] != 0) // result: 0 is rb-successful
            {
                return false;
            }
            userChannelId = static_cast<uint16_t>((pdu[2] << 8 | pdu[3]) + kIoChannelBase);
            return true;
        }

        Bytes BuildChannelJoinRequest(uint16_t userChannelId, uint16_t channelId)
        {
            Bytes out;
            out.push_back(0x38); // ChannelJoinRequest: choice 14 (0x0E << 2)
            uint16_t initiator = static_cast<uint16_t>(userChannelId - kIoChannelBase);
            out.push_back(static_cast<uint8_t>(initiator >> 8));
            out.push_back(static_cast<uint8_t>(initiator & 0xFF));
            out.push_back(static_cast<uint8_t>(channelId >> 8));
            out.push_back(static_cast<uint8_t>(channelId & 0xFF));
            return out;
        }

        bool ParseChannelJoinConfirm(const Bytes& pdu, uint16_t& channelId)
        {
            // ChannelJoinConfirm: choice 15 (0x3C), result, initiator (2), requested (2), channelId (2).
            if (pdu.size() < 8 || (pdu[0] & 0xFC) != 0x3C)
            {
                return false;
            }
            if (pdu[1] != 0)
            {
                return false;
            }
            channelId = static_cast<uint16_t>(pdu[6] << 8 | pdu[7]);
            return true;
        }

        Bytes BuildSendDataRequest(uint16_t userChannelId, uint16_t channelId, const Bytes& payload)
        {
            Bytes out;
            out.push_back(0x64); // SendDataRequest: choice 25 (0x19 << 2)
            uint16_t initiator = static_cast<uint16_t>(userChannelId - kIoChannelBase);
            out.push_back(static_cast<uint8_t>(initiator >> 8));
            out.push_back(static_cast<uint8_t>(initiator & 0xFF));
            out.push_back(static_cast<uint8_t>(channelId >> 8));
            out.push_back(static_cast<uint8_t>(channelId & 0xFF));
            out.push_back(0x70); // data priority / segmentation flags
            // Per-length of the payload (PER, may be long form).
            if (payload.size() < 0x80)
            {
                out.push_back(static_cast<uint8_t>(payload.size()));
            }
            else
            {
                out.push_back(static_cast<uint8_t>(0x80 | ((payload.size() >> 8) & 0x3F)));
                out.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
            }
            out.insert(out.end(), payload.begin(), payload.end());
            return out;
        }

        bool StripSendData(const Bytes& pdu, uint16_t& channelId, Bytes& payload)
        {
            // SendDataIndication: choice 26 (0x1A << 2 = 0x68).
            if (pdu.size() < 7 || (pdu[0] & 0xFC) != 0x68)
            {
                return false;
            }
            channelId = static_cast<uint16_t>(pdu[3] << 8 | pdu[4]);
            size_t p = 6; // skip choice(1), initiator(2), channelId(2), dataPriority/flags(1)
            size_t length = pdu[p++];
            if (length & 0x80)
            {
                if (p >= pdu.size())
                {
                    return false;
                }
                length = ((length & 0x3F) << 8) | pdu[p++];
            }
            if (p + length > pdu.size())
            {
                return false;
            }
            payload.assign(pdu.begin() + p, pdu.begin() + p + length);
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
