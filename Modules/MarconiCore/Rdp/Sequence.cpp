// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's connection-sequence PDUs after MCS: Client Info, the capability exchange, and finalisation.

#include <MarconiCore/Rdp/Sequence.h>

#include <cstdio>

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
            uint32_t GetU32(const Bytes& data, size_t offset)
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
                }
                return value;
            }

            // A UTF-16LE string with a trailing NUL; `fieldLength` (when building the Info packet) is the byte
            // count WITHOUT the terminator, as TS_INFO_PACKET wants.
            Bytes Utf16(const std::string& ascii)
            {
                Bytes out;
                for (char c : ascii)
                {
                    out.push_back(static_cast<uint8_t>(c));
                    out.push_back(0);
                }
                return out;
            }

            enum : uint16_t
            {
                kSecInfoPkt = 0x0040,
            };
            enum : uint32_t
            {
                kInfoMouse = 0x00000001,
                kInfoDisableCtrlAltDel = 0x00000002,
                kInfoAutoLogon = 0x00000008,
                kInfoUnicode = 0x00000010,
                kInfoMaximizeShell = 0x00000020,
                kInfoEnableWindowsKey = 0x00000100,
            };

            // Capability set types (MS-RDPBCGR 2.2.1.13.1.1.1).
            enum : uint16_t
            {
                kCapGeneral = 1,
                kCapBitmap = 2,
                kCapOrder = 3,
                kCapPointer = 8,
                kCapInput = 13,
                kCapBrush = 15,
                kCapGlyphCache = 16,
                kCapSound = 12,
                kCapShare = 9,
                kCapColorCache = 10,
            };

            // One capability set: its type, then the total length (header included), then the body.
            Bytes CapabilitySet(uint16_t type, const Bytes& body)
            {
                Bytes out;
                PutU16(out, type);
                PutU16(out, static_cast<uint16_t>(body.size() + 4));
                out.insert(out.end(), body.begin(), body.end());
                return out;
            }

            Bytes GeneralCapabilitySet()
            {
                Bytes body;
                PutU16(body, 1);  // osMajorType: Windows
                PutU16(body, 3);  // osMinorType: Windows NT
                PutU16(body, 0x0200); // protocolVersion: TS_CAPS_PROTOCOLVERSION
                PutU16(body, 0);  // pad
                PutU16(body, 0);  // generalCompressionTypes
                PutU16(body, 0x0004); // extraFlags: NO_BITMAP_COMPRESSION_HDR is cleared; FASTPATH not asked here
                PutU16(body, 0);  // updateCapabilityFlag
                PutU16(body, 0);  // remoteUnshareFlag
                PutU16(body, 0);  // generalCompressionLevel
                body.push_back(0); // refreshRectSupport
                body.push_back(0); // suppressOutputSupport
                return CapabilitySet(kCapGeneral, body);
            }

            Bytes BitmapCapabilitySet(uint16_t width, uint16_t height)
            {
                Bytes body;
                PutU16(body, 16);     // preferredBitsPerPixel
                PutU16(body, 1);      // receive1BitPerPixel
                PutU16(body, 1);      // receive4BitsPerPixel
                PutU16(body, 1);      // receive8BitsPerPixel
                PutU16(body, width);  // desktopWidth
                PutU16(body, height); // desktopHeight
                PutU16(body, 0);      // pad
                PutU16(body, 1);      // desktopResizeFlag
                PutU16(body, 1);      // bitmapCompressionFlag
                body.push_back(0);    // highColorFlags
                body.push_back(1);    // drawingFlags: DRAW_ALLOW_SKIP_ALPHA
                PutU16(body, 1);      // multipleRectangleSupport
                PutU16(body, 0);      // pad
                return CapabilitySet(kCapBitmap, body);
            }

            Bytes OrderCapabilitySet()
            {
                // The body is a fixed 84 bytes. We advertise no drawing orders (orderSupport, the 32 bytes at
                // offset 20, left zero), so the server falls back to the bitmap updates Marconi decodes. orderFlags
                // at offset 32 sets NEGOTIATEORDERSUPPORT (0x0002) and ZEROBOUNDSDELTASSUPPORT (0x0008).
                Bytes body(84, 0);
                body[32] = 0x0A;
                body[33] = 0x00;
                // maximumOrderLevel (offset 52) = 1 (ORD_LEVEL_1_ORDERS); textANSICodePage (offset 80) = 0.
                body[52] = 0x01;
                return CapabilitySet(kCapOrder, body);
            }

            Bytes PointerCapabilitySet()
            {
                Bytes body;
                PutU16(body, 1);   // colorPointerFlag
                PutU16(body, 25);  // colorPointerCacheSize
                PutU16(body, 25);  // pointerCacheSize
                return CapabilitySet(kCapPointer, body);
            }

            Bytes InputCapabilitySet(uint32_t keyboardLayout)
            {
                Bytes body;
                PutU16(body, 0x0001 | 0x0004 | 0x0008 | 0x0020); // inputFlags: scancodes, mousex, unicode, fastpath-in
                PutU16(body, 0);                                 // pad
                PutU32(body, keyboardLayout);
                PutU32(body, 4);  // keyboardType: enhanced
                PutU32(body, 0);  // keyboardSubType
                PutU32(body, 12); // keyboardFunctionKey
                body.insert(body.end(), 64, 0); // imeFileName
                return CapabilitySet(kCapInput, body);
            }

            Bytes ShareCapabilitySet()
            {
                Bytes body;
                PutU16(body, 0); // nodeId (filled by server for its own; 0 from client)
                PutU16(body, 0); // pad
                return CapabilitySet(kCapShare, body);
            }

            Bytes SmallCapabilitySet(uint16_t type, size_t bodyBytes)
            {
                return CapabilitySet(type, Bytes(bodyBytes, 0));
            }

            // A Share Data PDU body for the control PDUs.
            Bytes ControlBody(uint16_t action, uint16_t grantId, uint32_t controlId)
            {
                Bytes body;
                PutU16(body, action);
                PutU16(body, grantId);
                PutU32(body, controlId);
                return body;
            }
        } // namespace

        Bytes BuildClientInfo(const LogonInfo& logon)
        {
            Bytes info;
            PutU32(info, 0);          // codePage
            uint32_t flags = kInfoMouse | kInfoDisableCtrlAltDel | kInfoUnicode | kInfoMaximizeShell |
                             kInfoEnableWindowsKey;
            if (logon.autoLogon)
            {
                flags |= kInfoAutoLogon;
            }
            PutU32(info, flags);

            Bytes domain = Utf16(logon.domain);
            Bytes user = Utf16(logon.user);
            Bytes password = Utf16(logon.password);
            Bytes shell = Utf16(logon.shell);
            Bytes workingDir = Utf16(logon.workingDir);
            // Each length is the byte count without the 2-byte terminator.
            PutU16(info, static_cast<uint16_t>(domain.size()));
            PutU16(info, static_cast<uint16_t>(user.size()));
            PutU16(info, static_cast<uint16_t>(password.size()));
            PutU16(info, static_cast<uint16_t>(shell.size()));
            PutU16(info, static_cast<uint16_t>(workingDir.size()));
            auto append = [&info](const Bytes& field) {
                info.insert(info.end(), field.begin(), field.end());
                info.push_back(0); // the UTF-16 NUL terminator
                info.push_back(0);
            };
            append(domain);
            append(user);
            append(password);
            append(shell);
            append(workingDir);

            // TS_EXTENDED_INFO_PACKET (MS-RDPBCGR 2.2.1.11.1.1.1), required for RDP 5 and later. A minimal one: an
            // IPv4 client address and an empty client directory, both UTF-16 with a length that counts the NUL.
            Bytes clientAddress = Utf16("0.0.0.0");
            clientAddress.push_back(0);
            clientAddress.push_back(0);
            Bytes clientDir = {0, 0}; // just the NUL
            PutU16(info, 0x0002); // clientAddressFamily: AF_INET
            PutU16(info, static_cast<uint16_t>(clientAddress.size()));
            info.insert(info.end(), clientAddress.begin(), clientAddress.end());
            PutU16(info, static_cast<uint16_t>(clientDir.size()));
            info.insert(info.end(), clientDir.begin(), clientDir.end());
            // clientTimeZone (TS_TIME_ZONE_INFORMATION, 172 bytes), clientSessionId (4) and performanceFlags (4)
            // are all optional and read as zero; a block of zeros satisfies servers that expect them.
            info.insert(info.end(), 172 + 4 + 4, 0);

            Bytes out = BuildSecurityHeader(kSecInfoPkt);
            out.insert(out.end(), info.begin(), info.end());
            return out;
        }

        bool ParseDemandActive(const Bytes& pdu, uint32_t& shareId, uint16_t& capabilitySetCount)
        {
            ShareControlType type = ShareControlType::DemandActive;
            uint16_t source = 0;
            Bytes body;
            if (!ReadShareControl(pdu, type, source, body) || type != ShareControlType::DemandActive)
            {
                return false;
            }
            if (body.size() < 8)
            {
                return false;
            }
            shareId = GetU32(body, 0);
            uint16_t sourceDescriptorLength = GetU16(body, 4);
            size_t p = 6 + sourceDescriptorLength;
            if (p + 4 > body.size())
            {
                return false;
            }
            // lengthCombinedCapabilities (2), then numberCapabilities (2), then pad (2) follow the source descriptor.
            capabilitySetCount = GetU16(body, p + 2);
            return true;
        }

        bool ParseDemandActiveInfo(const Bytes& pdu, DemandActiveInfo& out)
        {
            out = DemandActiveInfo();
            if (!ParseDemandActive(pdu, out.shareId, out.capabilitySetCount))
            {
                return false;
            }
            ShareControlType type = ShareControlType::DemandActive;
            uint16_t source = 0;
            Bytes body;
            ReadShareControl(pdu, type, source, body);
            // shareId (4), lengthSourceDescriptor (2), lengthCombinedCapabilities (2), the source descriptor,
            // numberCapabilities (2), pad (2), then the capability sets.
            size_t p = 8 + GetU16(body, 4) + 4;
            for (uint16_t i = 0; i < out.capabilitySetCount && p + 4 <= body.size(); ++i)
            {
                uint16_t capType = GetU16(body, p);
                uint16_t length = GetU16(body, p + 2);
                if (length < 4 || p + length > body.size())
                {
                    break;
                }
                if (capType == kCapBitmap && length >= 16)
                {
                    out.preferredBitsPerPixel = GetU16(body, p + 4);
                    out.desktopWidth = GetU16(body, p + 12);
                    out.desktopHeight = GetU16(body, p + 14);
                }
                else if (capType == kCapGeneral && length >= 16)
                {
                    out.fastPathOutput = (GetU16(body, p + 14) & 0x0001) != 0; // FASTPATH_OUTPUT_SUPPORTED
                }
                p += length;
            }
            return true;
        }

        Bytes BuildConfirmActive(uint32_t shareId, uint16_t source, uint16_t desktopWidth, uint16_t desktopHeight)
        {
            Bytes caps;
            uint16_t count = 0;
            auto add = [&caps, &count](const Bytes& set) {
                caps.insert(caps.end(), set.begin(), set.end());
                ++count;
            };
            add(GeneralCapabilitySet());
            add(BitmapCapabilitySet(desktopWidth, desktopHeight));
            add(OrderCapabilitySet());
            add(PointerCapabilitySet());
            add(InputCapabilitySet(0x409));
            add(ShareCapabilitySet());
            add(SmallCapabilitySet(kCapBrush, 4));
            add(SmallCapabilitySet(kCapGlyphCache, 48));
            add(SmallCapabilitySet(kCapSound, 4));
            add(SmallCapabilitySet(kCapColorCache, 4));

            Bytes body;
            PutU32(body, shareId);
            PutU16(body, 0x03EA); // originatorId: the server channel
            const char* source2 = "MARCONI";
            PutU16(body, 8);      // lengthSourceDescriptor
            body.insert(body.end(), source2, source2 + 7);
            body.push_back(0);
            PutU16(body, static_cast<uint16_t>(caps.size() + 4)); // lengthCombinedCapabilities
            PutU16(body, count);  // numberCapabilities
            PutU16(body, 0);      // pad
            body.insert(body.end(), caps.begin(), caps.end());
            return WrapShareControl(ShareControlType::ConfirmActive, source, body);
        }

        Bytes BuildSynchronize(uint32_t shareId, uint16_t source, uint16_t targetUser)
        {
            Bytes body;
            PutU16(body, 1); // messageType: SYNCMSGTYPE_SYNC
            PutU16(body, targetUser);
            return WrapShareData(shareId, source, ShareDataType::Synchronize, body);
        }

        Bytes BuildControlCooperate(uint32_t shareId, uint16_t source)
        {
            return WrapShareData(shareId, source, ShareDataType::Control, ControlBody(4, 0, 0)); // CTRLACTION_COOPERATE
        }

        Bytes BuildControlRequest(uint32_t shareId, uint16_t source)
        {
            return WrapShareData(shareId, source, ShareDataType::Control, ControlBody(1, 0, 0)); // REQUEST_CONTROL
        }

        Bytes BuildFontList(uint32_t shareId, uint16_t source)
        {
            Bytes body;
            PutU16(body, 0);      // numberFonts
            PutU16(body, 0);      // totalNumberFonts
            PutU16(body, 0x0003); // listFlags: FONTLIST_FIRST | FONTLIST_LAST
            PutU16(body, 50);     // entrySize
            return WrapShareData(shareId, source, ShareDataType::FontList, body);
        }

        Bytes BuildRefreshRect(uint32_t shareId, uint16_t source, uint16_t width, uint16_t height)
        {
            Bytes body;
            body.push_back(1); // numberOfAreas
            body.insert(body.end(), 3, 0); // pad
            PutU16(body, 0); // left
            PutU16(body, 0); // top
            PutU16(body, static_cast<uint16_t>(width > 0 ? width - 1 : 0));   // right, inclusive
            PutU16(body, static_cast<uint16_t>(height > 0 ? height - 1 : 0)); // bottom, inclusive
            return WrapShareData(shareId, source, ShareDataType::RefreshRect, body);
        }

        bool ParseSetErrorInfo(const Bytes& body, uint32_t& errorInfo)
        {
            if (body.size() < 4)
            {
                return false;
            }
            errorInfo = GetU32(body, 0);
            return true;
        }

        std::string ErrorInfoText(uint32_t errorInfo)
        {
            switch (errorInfo)
            {
                case 0x00000001: return "The server ended the connection.";
                case 0x00000002: return "The server logged this session off.";
                case 0x00000003: return "The server ended the connection because it was idle for too long.";
                case 0x00000004: return "The server ended the connection because nobody logged in for too long.";
                case 0x00000005: return "Another connection to that computer took over this session.";
                case 0x00000006: return "The server ran out of memory.";
                case 0x00000007: return "The server refused the connection.";
                case 0x00000009: return "That account isn't allowed to connect to this computer remotely.";
                case 0x0000000A: return "The server wants you to log in again.";
                case 0x0000000B: return "Someone on the other computer ended the connection.";
                case 0x0000000C: return "You were logged off.";
                case 0x00000011: return "The server has no licence left for this connection.";
                default: break;
            }
            char code[32];
            snprintf(code, sizeof(code), "0x%08x", errorInfo);
            return std::string("The server ended the connection (code ") + code + ").";
        }
    } // namespace Rdp
} // namespace Marconi
