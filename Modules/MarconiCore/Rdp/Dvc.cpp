// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Static channel chunking and the DRDYNVC dynamic channel PDUs.

#include <MarconiCore/Rdp/Dvc.h>

#include <algorithm>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr uint32_t kChannelFlagFirst = 0x01;
            constexpr uint32_t kChannelFlagLast = 0x02;
            constexpr uint32_t kChannelPacketCompressed = 0x00200000;
            constexpr size_t kMaxChannelMessage = 64 * 1024 * 1024;
            constexpr size_t kDvcPduSize = 1600; // the largest DRDYNVC PDU, header included

            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>(value >> 8));
            }
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }
            uint32_t GetU32(const Bytes& data, size_t offset)
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
                }
                return value;
            }

            // DRDYNVC's variable-size fields: a 2-bit size code (0: 1 byte, 1: 2 bytes, 2: 4 bytes).
            bool GetVariable(const Bytes& data, size_t& p, int sizeCode, uint32_t& value)
            {
                size_t bytes = sizeCode == 0 ? 1 : sizeCode == 1 ? 2 : sizeCode == 2 ? 4 : 0;
                if (bytes == 0 || p + bytes > data.size())
                {
                    return false;
                }
                value = 0;
                for (size_t i = 0; i < bytes; ++i)
                {
                    value |= static_cast<uint32_t>(data[p + i]) << (8 * i);
                }
                p += bytes;
                return true;
            }
            int SizeCode(uint32_t value)
            {
                return value <= 0xFF ? 0 : value <= 0xFFFF ? 1 : 2;
            }
            void PutVariable(Bytes& out, int sizeCode, uint32_t value)
            {
                size_t bytes = sizeCode == 0 ? 1 : sizeCode == 1 ? 2 : 4;
                for (size_t i = 0; i < bytes; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }
            uint8_t Header(DvcCommand command, int sp, int cbChId)
            {
                return static_cast<uint8_t>((static_cast<uint8_t>(command) << 4) | (sp << 2) | cbChId);
            }
        } // namespace

        bool ChannelReassembler::Add(const Bytes& payload, Bytes& message, std::string& error)
        {
            error.clear();
            if (payload.size() < 8)
            {
                error = "a virtual channel message too short for its header";
                return false;
            }
            uint32_t total = GetU32(payload, 0);
            uint32_t flags = GetU32(payload, 4);
            if (flags & kChannelPacketCompressed)
            {
                error = "a compressed virtual channel message, which Marconi didn't ask for";
                return false;
            }
            if (total > kMaxChannelMessage)
            {
                error = "a virtual channel message too large to be real";
                return false;
            }
            if (flags & kChannelFlagFirst)
            {
                m_pending.clear();
                m_expected = total;
                m_inMessage = true;
            }
            else if (!m_inMessage)
            {
                error = "a virtual channel chunk that doesn't follow a first chunk";
                return false;
            }
            m_pending.insert(m_pending.end(), payload.begin() + 8, payload.end());
            if (m_pending.size() > m_expected)
            {
                m_inMessage = false;
                error = "a virtual channel message longer than it declared";
                return false;
            }
            if (!(flags & kChannelFlagLast))
            {
                return false;
            }
            m_inMessage = false;
            if (m_pending.size() != m_expected)
            {
                error = "a virtual channel message shorter than it declared";
                return false;
            }
            message.swap(m_pending);
            m_pending.clear();
            return true;
        }

        std::vector<Bytes> BuildChannelChunks(const Bytes& message)
        {
            std::vector<Bytes> chunks;
            size_t offset = 0;
            do
            {
                size_t length = std::min(kChannelChunkSize, message.size() - offset);
                uint32_t flags = 0;
                if (offset == 0)
                {
                    flags |= kChannelFlagFirst;
                }
                if (offset + length == message.size())
                {
                    flags |= kChannelFlagLast;
                }
                Bytes chunk;
                PutU32(chunk, static_cast<uint32_t>(message.size()));
                PutU32(chunk, flags);
                chunk.insert(chunk.end(), message.begin() + offset, message.begin() + offset + length);
                chunks.push_back(std::move(chunk));
                offset += length;
            } while (offset < message.size());
            return chunks;
        }

        bool ParseDvcPdu(const Bytes& message, DvcPdu& out, std::string& error)
        {
            out = DvcPdu();
            if (message.empty())
            {
                error = "an empty dynamic channel message";
                return false;
            }
            uint8_t header = message[0];
            int cbChId = header & 0x03;
            int sp = (header >> 2) & 0x03;
            out.command = static_cast<DvcCommand>(header >> 4);
            size_t p = 1;
            switch (out.command)
            {
                case DvcCommand::Capability:
                    // A pad byte, the version, then (versions 2 and 3) priority charges this client doesn't use.
                    if (message.size() < 4)
                    {
                        error = "a dynamic channel capability request too short";
                        return false;
                    }
                    out.version = static_cast<uint16_t>(message[2] | (message[3] << 8));
                    return true;
                case DvcCommand::Create:
                {
                    if (!GetVariable(message, p, cbChId, out.channelId))
                    {
                        error = "a dynamic channel create request too short";
                        return false;
                    }
                    size_t end = p;
                    while (end < message.size() && message[end] != 0)
                    {
                        ++end;
                    }
                    out.channelName.assign(message.begin() + p, message.begin() + end);
                    return true;
                }
                case DvcCommand::DataFirst:
                    if (!GetVariable(message, p, cbChId, out.channelId) || !GetVariable(message, p, sp, out.totalLength))
                    {
                        error = "a dynamic channel data message too short";
                        return false;
                    }
                    out.data.assign(message.begin() + p, message.end());
                    return true;
                case DvcCommand::Data:
                    if (!GetVariable(message, p, cbChId, out.channelId))
                    {
                        error = "a dynamic channel data message too short";
                        return false;
                    }
                    out.data.assign(message.begin() + p, message.end());
                    return true;
                case DvcCommand::Close:
                    if (!GetVariable(message, p, cbChId, out.channelId))
                    {
                        error = "a dynamic channel close request too short";
                        return false;
                    }
                    return true;
                default:
                    // Compressed data (version 3, which Marconi doesn't offer) and the soft-sync messages of
                    // multitransport: recognised, with nothing parsed.
                    return true;
            }
        }

        Bytes BuildDvcCapabilityResponse(uint16_t version)
        {
            Bytes out;
            out.push_back(Header(DvcCommand::Capability, 0, 0));
            out.push_back(0); // pad
            PutU16(out, version);
            return out;
        }

        Bytes BuildDvcCreateResponse(uint32_t channelId, int32_t creationStatus)
        {
            Bytes out;
            int cb = SizeCode(channelId);
            out.push_back(Header(DvcCommand::Create, 0, cb));
            PutVariable(out, cb, channelId);
            PutU32(out, static_cast<uint32_t>(creationStatus));
            return out;
        }

        Bytes BuildDvcClose(uint32_t channelId)
        {
            Bytes out;
            int cb = SizeCode(channelId);
            out.push_back(Header(DvcCommand::Close, 0, cb));
            PutVariable(out, cb, channelId);
            return out;
        }

        std::vector<Bytes> BuildDvcData(uint32_t channelId, const Bytes& data)
        {
            std::vector<Bytes> pdus;
            int cb = SizeCode(channelId);
            size_t idBytes = cb == 0 ? 1 : cb == 1 ? 2 : 4;
            if (1 + idBytes + data.size() <= kDvcPduSize)
            {
                Bytes pdu;
                pdu.push_back(Header(DvcCommand::Data, 0, cb));
                PutVariable(pdu, cb, channelId);
                pdu.insert(pdu.end(), data.begin(), data.end());
                pdus.push_back(std::move(pdu));
                return pdus;
            }
            int sp = SizeCode(static_cast<uint32_t>(data.size()));
            size_t lengthBytes = sp == 0 ? 1 : sp == 1 ? 2 : 4;
            Bytes first;
            first.push_back(Header(DvcCommand::DataFirst, sp, cb));
            PutVariable(first, cb, channelId);
            PutVariable(first, sp, static_cast<uint32_t>(data.size()));
            size_t take = kDvcPduSize - 1 - idBytes - lengthBytes;
            first.insert(first.end(), data.begin(), data.begin() + take);
            pdus.push_back(std::move(first));
            size_t offset = take;
            while (offset < data.size())
            {
                size_t length = std::min(kDvcPduSize - 1 - idBytes, data.size() - offset);
                Bytes pdu;
                pdu.push_back(Header(DvcCommand::Data, 0, cb));
                PutVariable(pdu, cb, channelId);
                pdu.insert(pdu.end(), data.begin() + offset, data.begin() + offset + length);
                pdus.push_back(std::move(pdu));
                offset += length;
            }
            return pdus;
        }

        bool DvcReassembler::Add(const DvcPdu& pdu, Bytes& message, std::string& error)
        {
            error.clear();
            if (pdu.command == DvcCommand::DataFirst)
            {
                if (pdu.totalLength > kMaxChannelMessage || pdu.data.size() > pdu.totalLength)
                {
                    m_pending.erase(pdu.channelId);
                    error = "a dynamic channel message with an impossible length";
                    return false;
                }
                if (pdu.data.size() == pdu.totalLength)
                {
                    m_pending.erase(pdu.channelId);
                    message = pdu.data;
                    return true;
                }
                Pending& pending = m_pending[pdu.channelId];
                pending.expected = pdu.totalLength;
                pending.data = pdu.data;
                return false;
            }
            auto it = m_pending.find(pdu.channelId);
            if (it == m_pending.end())
            {
                message = pdu.data; // a whole message in one Data PDU
                return true;
            }
            Pending& pending = it->second;
            pending.data.insert(pending.data.end(), pdu.data.begin(), pdu.data.end());
            if (pending.data.size() > pending.expected)
            {
                m_pending.erase(it);
                error = "a dynamic channel message longer than it declared";
                return false;
            }
            if (pending.data.size() < pending.expected)
            {
                return false;
            }
            message.swap(pending.data);
            m_pending.erase(it);
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
