// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP PDU framing and the connection-sequence PDUs (MS-RDPBCGR 2.2.8.1, 2.2.1.11-19). Structures are
// checked by round-trip and known layout; the full exchange needs a real server.

#include "TestSupport.h"

#include <MarconiCore/Rdp/Pdu.h>
#include <MarconiCore/Rdp/Sequence.h>

#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        uint16_t U16(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o]) | (static_cast<uint16_t>(b[o + 1]) << 8); }
        uint32_t U32(const Rdp::Bytes& b, size_t o)
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[o + i]) << (8 * i);
            return v;
        }
    } // namespace

    void RunRdpSequenceTests()
    {
        RunTest("Share Control framing round-trips type, source and body", []() {
            Rdp::Bytes body = {10, 20, 30, 40};
            Rdp::Bytes pdu = Rdp::WrapShareControl(Rdp::ShareControlType::ConfirmActive, 1007, body);
            CHECK(U16(pdu, 0) == pdu.size());        // totalLength
            CHECK((U16(pdu, 2) & 0x000F) == 3);      // ConfirmActive
            CHECK((U16(pdu, 2) & 0xFFF0) == 0x0010); // protocol version
            Rdp::ShareControlType type;
            uint16_t source = 0;
            Rdp::Bytes got;
            CHECK(Rdp::ReadShareControl(pdu, type, source, got));
            CHECK(type == Rdp::ShareControlType::ConfirmActive && source == 1007 && got == body);
        });

        RunTest("Share Data framing round-trips the type and body", []() {
            Rdp::Bytes body = {1, 2, 3};
            Rdp::Bytes pdu = Rdp::WrapShareData(0x12345678, 1007, Rdp::ShareDataType::FontList, body);
            Rdp::ShareControlType controlType;
            uint16_t source = 0;
            Rdp::Bytes control;
            CHECK(Rdp::ReadShareControl(pdu, controlType, source, control));
            CHECK(controlType == Rdp::ShareControlType::Data);
            CHECK(U32(control, 0) == 0x12345678); // shareId
            Rdp::ShareDataType type2;
            Rdp::Bytes got;
            CHECK(Rdp::ReadShareData(pdu, type2, got));
            CHECK(type2 == Rdp::ShareDataType::FontList && got == body);
        });

        RunTest("The Client Info packet carries the credentials UTF-16 with their lengths", []() {
            Rdp::LogonInfo logon;
            logon.domain = "WORK";
            logon.user = "alice";
            logon.password = "pw";
            Rdp::Bytes pdu = Rdp::BuildClientInfo(logon);
            CHECK(U16(pdu, 0) == 0x0040); // SEC_INFO_PKT in the security header
            size_t info = 4;              // after the 4-byte security header
            // codePage(4), flags(4), then the five lengths.
            CHECK(U16(pdu, info + 8) == 8);  // domain "WORK" = 8 bytes UTF-16, no terminator
            CHECK(U16(pdu, info + 10) == 10); // user "alice" = 10 bytes
            CHECK(U16(pdu, info + 12) == 4);  // password "pw" = 4 bytes
            uint32_t flags = U32(pdu, info + 4);
            CHECK((flags & 0x00000010) != 0); // INFO_UNICODE
            CHECK((flags & 0x00000008) != 0); // INFO_AUTOLOGON (autoLogon defaults true)
            // The domain text follows the five 2-byte lengths: 'W',0,'O',0...
            CHECK(pdu[info + 18] == 'W' && pdu[info + 19] == 0 && pdu[info + 20] == 'O');
        });

        RunTest("A Demand Active PDU is parsed for its share id and capability count", []() {
            // Build a minimal Demand Active by hand: shareId, a short source descriptor, counts, then no sets.
            Rdp::Bytes body;
            auto p16 = [&](uint16_t v) { body.push_back(static_cast<uint8_t>(v)); body.push_back(static_cast<uint8_t>(v >> 8)); };
            auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) body.push_back(static_cast<uint8_t>(v >> (8 * i))); };
            p32(0x000A0001);  // shareId
            p16(4);           // lengthSourceDescriptor
            body.insert(body.end(), {'R', 'D', 'P', 0});
            p16(4);           // lengthCombinedCapabilities
            p16(5);           // numberCapabilities
            p16(0);           // pad
            Rdp::Bytes pdu = Rdp::WrapShareControl(Rdp::ShareControlType::DemandActive, 0x03EA, body);
            uint32_t shareId = 0;
            uint16_t count = 0;
            CHECK(Rdp::ParseDemandActive(pdu, shareId, count));
            CHECK(shareId == 0x000A0001 && count == 5);
            // A non-demand-active PDU is rejected.
            CHECK(!Rdp::ParseDemandActive(Rdp::WrapShareControl(Rdp::ShareControlType::Data, 0x03EA, body), shareId, count));
        });

        RunTest("The Confirm Active PDU lists the client capability sets", []() {
            Rdp::Bytes pdu = Rdp::BuildConfirmActive(0x000A0001, 1007, 1280, 1024);
            Rdp::ShareControlType type;
            uint16_t source = 0;
            Rdp::Bytes body;
            CHECK(Rdp::ReadShareControl(pdu, type, source, body));
            CHECK(type == Rdp::ShareControlType::ConfirmActive && source == 1007);
            CHECK(U32(body, 0) == 0x000A0001); // shareId echoed
            uint16_t sourceLen = U16(body, 6);
            size_t p = 8 + sourceLen;
            uint16_t numberCaps = U16(body, p + 2);
            CHECK(numberCaps >= 6); // general, bitmap, order, pointer, input, share at least
            // The first capability set is General (type 1), and the bitmap set carries the desktop size.
            size_t caps = p + 6;
            CHECK(U16(body, caps) == 1); // kCapGeneral
            // Walk to the Bitmap set (type 2) and check the width/height.
            size_t q = caps;
            bool foundBitmap = false;
            for (uint16_t i = 0; i < numberCaps && q + 4 <= body.size(); ++i)
            {
                uint16_t capType = U16(body, q);
                uint16_t capLen = U16(body, q + 2);
                if (capType == 2 && capLen >= 12)
                {
                    CHECK(U16(body, q + 4 + 8) == 1280);  // desktopWidth
                    CHECK(U16(body, q + 4 + 10) == 1024); // desktopHeight
                    foundBitmap = true;
                }
                if (capLen < 4) break;
                q += capLen;
            }
            CHECK(foundBitmap);
        });

        RunTest("The finalisation PDUs are data PDUs of the right kind", []() {
            uint32_t shareId = 0x000A0001;
            Rdp::ShareDataType type2;
            Rdp::Bytes body;
            CHECK(Rdp::ReadShareData(Rdp::BuildSynchronize(shareId, 1007, 0x03EA), type2, body));
            CHECK(type2 == Rdp::ShareDataType::Synchronize && U16(body, 0) == 1);
            CHECK(Rdp::ReadShareData(Rdp::BuildControlCooperate(shareId, 1007), type2, body));
            CHECK(type2 == Rdp::ShareDataType::Control && U16(body, 0) == 4); // CTRLACTION_COOPERATE
            CHECK(Rdp::ReadShareData(Rdp::BuildControlRequest(shareId, 1007), type2, body));
            CHECK(type2 == Rdp::ShareDataType::Control && U16(body, 0) == 1); // REQUEST_CONTROL
            CHECK(Rdp::ReadShareData(Rdp::BuildFontList(shareId, 1007), type2, body));
            CHECK(type2 == Rdp::ShareDataType::FontList);
        });
    }
} // namespace MarconiTest
