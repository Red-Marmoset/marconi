// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's connection-sequence PDUs after MCS: Client Info, the capability exchange, and finalisation.

#include <MarconiCore/Rdp/Sequence.h>

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
    } // namespace Rdp
} // namespace Marconi
