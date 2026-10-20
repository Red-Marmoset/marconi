// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for NTLMv2, checked against the worked example in MS-NLMP 4.2.4 (user "User", domain "Domain", password
// "Password", server challenge 0123456789abcdef, client challenge aaaaaaaaaaaaaaaa), whose published intermediate
// values pin the computation exactly.

#include "TestSupport.h"

#include <MarconiCore/Rdp/Ntlm.h>

#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        std::string Hex(const Rdp::Bytes& bytes)
        {
            static const char* digits = "0123456789abcdef";
            std::string out;
            for (uint8_t b : bytes)
            {
                out.push_back(digits[b >> 4]);
                out.push_back(digits[b & 15]);
            }
            return out;
        }

        // The target info (AV pairs) from MS-NLMP 4.2.4.1.3: NetBIOS domain "Domain", computer "Server", then EOL.
        Rdp::Bytes ExampleTargetInfo()
        {
            return {0x02, 0x00, 0x0c, 0x00, 0x44, 0x00, 0x6f, 0x00, 0x6d, 0x00, 0x61, 0x00, 0x69, 0x00, 0x6e, 0x00,
                    0x01, 0x00, 0x0c, 0x00, 0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00,
                    0x00, 0x00, 0x00, 0x00};
        }
    } // namespace

    void RunRdpNtlmTests()
    {
        RunTest("NTOWFv2 matches the MS-NLMP worked example", []() {
            CHECK(Hex(Rdp::NtowfV2("User", "Domain", "Password")) == "0c868a403bfd7a93a3001ef22ef02e3f");
            // Uppercasing is applied to the user only: the domain's case is kept.
            CHECK(Hex(Rdp::NtowfV2("user", "Domain", "Password")) == "0c868a403bfd7a93a3001ef22ef02e3f");
            CHECK(Hex(Rdp::NtowfV2("User", "domain", "Password")) != "0c868a403bfd7a93a3001ef22ef02e3f");
        });

        RunTest("The NTLMv2 response's proof string and session key match the worked example", []() {
            uint8_t serverChallenge[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
            uint8_t clientChallenge[8] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
            Rdp::Ntlmv2Response response = Rdp::ComputeNtlmv2("User", "Domain", "Password", serverChallenge,
                                                             clientChallenge, 0, ExampleTargetInfo());
            // The first 16 bytes of the response are NTProofStr (MS-NLMP 4.2.4.2.2).
            Rdp::Bytes ntProof(response.response.begin(), response.response.begin() + 16);
            CHECK(Hex(ntProof) == "68cd0ab851e51c96aabc927bebef6a1c");
            CHECK(Hex(response.sessionBaseKey) == "8de40ccadbc14a82f15cb0ad0de95ca3");
            // The blob after the proof string begins with the version/reserved and carries the challenge back.
            CHECK(response.response.size() == 16u + 32u + ExampleTargetInfo().size());
            CHECK(response.response[16] == 0x01 && response.response[17] == 0x01);
        });

        RunTest("The three NTLM messages carry their signature and type", []() {
            Rdp::Bytes negotiate = Rdp::BuildNegotiateMessage();
            CHECK(negotiate.size() >= 32);
            CHECK(std::string(negotiate.begin(), negotiate.begin() + 7) == "NTLMSSP");
            CHECK(negotiate[8] == 1 && negotiate[9] == 0 && negotiate[10] == 0 && negotiate[11] == 0);

            Rdp::Bytes nt(28, 0x11); // a stand-in NtChallengeResponse
            Rdp::Bytes auth = Rdp::BuildAuthenticateMessage("User", "Domain", "WS", nt, {}, 0x00080201);
            CHECK(std::string(auth.begin(), auth.begin() + 7) == "NTLMSSP");
            CHECK(auth[8] == 3);
            // The MIC's 16 bytes sit at the reserved offset, zero until filled in.
            for (size_t i = Rdp::MicOffset(); i < Rdp::MicOffset() + 16; ++i)
            {
                CHECK(auth[i] == 0);
            }
            // The NtChallengeResponse is where its field descriptor says, byte for byte.
            uint16_t ntLen = static_cast<uint16_t>(auth[20]) | (static_cast<uint16_t>(auth[21]) << 8);
            uint32_t ntOff = 0;
            for (int i = 0; i < 4; ++i)
            {
                ntOff |= static_cast<uint32_t>(auth[24 + i]) << (8 * i);
            }
            CHECK(ntLen == nt.size());
            CHECK(ntOff + ntLen <= auth.size());
            CHECK(Rdp::Bytes(auth.begin() + ntOff, auth.begin() + ntOff + ntLen) == nt);
        });

        RunTest("A challenge message is parsed back into its server challenge and target info", []() {
            // Build a minimal, well-formed CHALLENGE and read it back.
            Rdp::Bytes targetInfo = ExampleTargetInfo();
            Rdp::Bytes message;
            const char* sig = "NTLMSSP";
            message.insert(message.end(), sig, sig + 8);
            auto put32 = [&message](uint32_t v) { for (int i = 0; i < 4; ++i) message.push_back(static_cast<uint8_t>(v >> (8 * i))); };
            auto put16 = [&message](uint16_t v) { message.push_back(static_cast<uint8_t>(v)); message.push_back(static_cast<uint8_t>(v >> 8)); };
            put32(2);        // type
            put16(0); put16(0); put32(48); // target name fields (empty)
            put32(0x00080205);             // flags
            uint8_t challenge[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
            message.insert(message.end(), challenge, challenge + 8); // server challenge
            put32(0); put32(0);            // reserved
            put16(static_cast<uint16_t>(targetInfo.size()));
            put16(static_cast<uint16_t>(targetInfo.size()));
            put32(48);                     // target info offset (right after this 48-byte header, no version field)
            message.insert(message.end(), targetInfo.begin(), targetInfo.end());

            Rdp::ChallengeMessage parsed;
            CHECK(Rdp::ParseChallengeMessage(message, parsed));
            CHECK(Rdp::Bytes(parsed.serverChallenge, parsed.serverChallenge + 8) ==
                  Rdp::Bytes(challenge, challenge + 8));
            CHECK(parsed.targetInfo == targetInfo);
            CHECK(parsed.flags == 0x00080205);

            Rdp::Bytes tooShort(20, 0);
            CHECK(!Rdp::ParseChallengeMessage(tooShort, parsed));
        });
    }
} // namespace MarconiTest
