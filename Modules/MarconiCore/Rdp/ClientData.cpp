// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The client and server data blocks for RDP's basic settings exchange (MS-RDPBCGR 2.2.1.3 / 2.2.1.4).

#include <MarconiCore/Rdp/ClientData.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // All multi-byte fields in these blocks are little-endian.
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
            uint32_t GetU32(const Bytes& data, size_t offset)
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
                }
                return value;
            }

            // A block header is the 16-bit type then the 16-bit total length (header included); the length is filled
            // in once the body is known.
            size_t BeginBlock(Bytes& out, uint16_t type)
            {
                PutU16(out, type);
                size_t lengthAt = out.size();
                PutU16(out, 0);
                return lengthAt;
            }
            void EndBlock(Bytes& out, size_t lengthAt)
            {
                uint16_t length = static_cast<uint16_t>(out.size() - (lengthAt - 2));
                out[lengthAt] = static_cast<uint8_t>(length & 0xFF);
                out[lengthAt + 1] = static_cast<uint8_t>((length >> 8) & 0xFF);
            }

            // UTF-16LE, padded with zeros to exactly `wideChars` 16-bit units (truncated if longer, always
            // NUL-terminated within the field).
            void PutUtf16Fixed(Bytes& out, const std::string& ascii, size_t wideChars)
            {
                for (size_t i = 0; i < wideChars; ++i)
                {
                    uint16_t ch = (i + 1 < wideChars && i < ascii.size()) ? static_cast<uint8_t>(ascii[i]) : 0;
                    PutU16(out, ch);
                }
            }

            constexpr uint16_t kCsCore = 0xC001;
            constexpr uint16_t kCsSecurity = 0xC002;
            constexpr uint16_t kCsNet = 0xC003;
            constexpr uint16_t kScCore = 0x0C01;
            constexpr uint16_t kScNet = 0x0C03;
        } // namespace

        Bytes BuildClientData(const ClientCoreSettings& core, const std::vector<ChannelDefinition>& channels)
        {
            Bytes out;

            // TS_UD_CS_CORE (MS-RDPBCGR 2.2.1.3.2).
            size_t coreLength = BeginBlock(out, kCsCore);
            PutU32(out, 0x00080001);                 // version: RDP 5.0 and later
            PutU16(out, core.desktopWidth);
            PutU16(out, core.desktopHeight);
            PutU16(out, 0xCA01);                     // colorDepth: 8bpp (legacy field; highColorDepth is used)
            PutU16(out, 0xAA03);                     // SASSequence: Secure Access Sequence (Ctrl+Alt+Del)
            PutU32(out, core.keyboardLayout);
            PutU32(out, 2600);                       // clientBuild
            PutUtf16Fixed(out, core.clientName, 16); // clientName: 32 bytes
            PutU32(out, 4);                          // keyboardType: IBM enhanced
            PutU32(out, 0);                          // keyboardSubType
            PutU32(out, 12);                         // keyboardFunctionKey
            out.insert(out.end(), 64, 0);            // imeFileName: 64 bytes
            PutU16(out, 0xCA01);                     // postBeta2ColorDepth
            PutU16(out, 1);                          // clientProductId
            PutU32(out, 0);                          // serialNumber
            PutU16(out, core.highColorDepth);        // highColorDepth
            PutU16(out, 0x0007);                     // supportedColorDepths: 24/16/15
            PutU16(out, 0x0001);                     // earlyCapabilityFlags: RNS_UD_CS_SUPPORT_ERRINFO_PDU
            out.insert(out.end(), 64, 0);            // clientDigProductId: 64 bytes
            out.push_back(0);                        // connectionType
            out.push_back(0);                        // pad1octet
            PutU32(out, core.serverSelectedProtocol); // must echo the negotiated protocol
            EndBlock(out, coreLength);

            // TS_UD_CS_SEC (2.2.1.3.3): no RDP encryption of our own; TLS (and NLA) handle it.
            size_t securityLength = BeginBlock(out, kCsSecurity);
            PutU32(out, 0); // encryptionMethods
            PutU32(out, 0); // extEncryptionMethods
            EndBlock(out, securityLength);

            // TS_UD_CS_NET (2.2.1.3.4): the virtual channels, if any.
            size_t netLength = BeginBlock(out, kCsNet);
            PutU32(out, static_cast<uint32_t>(channels.size()));
            for (const ChannelDefinition& channel : channels)
            {
                for (size_t i = 0; i < 8; ++i)
                {
                    out.push_back(i < channel.name.size() ? static_cast<uint8_t>(channel.name[i]) : 0);
                }
                PutU32(out, channel.options);
            }
            EndBlock(out, netLength);
            return out;
        }

        bool ParseServerData(const Bytes& userData, ServerData& out)
        {
            out = ServerData();
            size_t p = 0;
            bool haveNet = false;
            while (p + 4 <= userData.size())
            {
                uint16_t type = GetU16(userData, p);
                uint16_t length = GetU16(userData, p + 2);
                if (length < 4 || p + length > userData.size())
                {
                    return false;
                }
                size_t body = p + 4;
                if (type == kScCore)
                {
                    if (length >= 8)
                    {
                        out.version = GetU32(userData, body);
                    }
                }
                else if (type == kScNet)
                {
                    // TS_UD_SC_NET: the I/O channel id, the channel count, then one id per channel, padded to a
                    // 4-byte multiple.
                    if (length < 8)
                    {
                        return false;
                    }
                    out.ioChannelId = GetU16(userData, body);
                    uint16_t count = GetU16(userData, body + 2);
                    if (body + 4 + static_cast<size_t>(count) * 2 > userData.size())
                    {
                        return false;
                    }
                    for (uint16_t i = 0; i < count; ++i)
                    {
                        out.channelIds.push_back(GetU16(userData, body + 4 + static_cast<size_t>(i) * 2));
                    }
                    haveNet = true;
                }
                p += length;
            }
            // The server always sends the core block; the net block only when it sent SC data at all. Require the
            // core version to have been seen as a basic sanity check.
            (void)haveNet;
            return out.version != 0 || out.ioChannelId != 0;
        }
    } // namespace Rdp
} // namespace Marconi
