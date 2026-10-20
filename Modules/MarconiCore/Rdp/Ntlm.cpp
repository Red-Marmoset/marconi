// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NTLMv2 for RDP's Network Level Authentication (CredSSP). Client side only, NTLMv2 only. Follows MS-NLMP; the
// crypto comes from the engine (Md4, HmacMd5, Md5, Rc4, RandomBytes).

#include <MarconiCore/Rdp/Ntlm.h>

#include <Crypto/Crypto.h>

#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // NTLMSSP negotiate flags (MS-NLMP 2.2.2.5), the ones Marconi uses.
            enum : uint32_t
            {
                kNegotiateUnicode = 0x00000001,
                kNegotiateRequestTarget = 0x00000004,
                kNegotiateSign = 0x00000010,
                kNegotiateSeal = 0x00000020,
                kNegotiateNtlm = 0x00000200,
                kNegotiateAlwaysSign = 0x00008000,
                kNegotiateExtendedSessionSecurity = 0x00080000,
                kNegotiateVersion = 0x02000000,
                kNegotiate128 = 0x20000000,
                kNegotiateKeyExchange = 0x40000000,
                kNegotiate56 = 0x80000000,
            };

            const char kSignature[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', '\0'};

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
            void PutU64(Bytes& out, uint64_t value)
            {
                for (int i = 0; i < 8; ++i)
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

            Bytes Utf16Le(const std::string& ascii)
            {
                Bytes out;
                for (char c : ascii)
                {
                    out.push_back(static_cast<uint8_t>(c));
                    out.push_back(0x00); // ASCII is enough for user names and domains in practice
                }
                return out;
            }

            std::string UpperAscii(const std::string& text)
            {
                std::string out = text;
                for (char& c : out)
                {
                    if (c >= 'a' && c <= 'z')
                    {
                        c = static_cast<char>(c - 'a' + 'A');
                    }
                }
                return out;
            }

            Bytes HmacMd5(const Bytes& key, const uint8_t* data, size_t length)
            {
                Bytes out(16);
                Crypto::HmacMd5(key.data(), key.size(), data, length, out.data());
                return out;
            }
            Bytes HmacMd5(const Bytes& key, const Bytes& data) { return HmacMd5(key, data.data(), data.size()); }
        } // namespace

        Bytes NtowfV2(const std::string& user, const std::string& domain, const std::string& password)
        {
            Bytes unicodePassword = Utf16Le(password);
            uint8_t ntowf1[16];
            Crypto::Md4(unicodePassword.data(), unicodePassword.size(), ntowf1);
            Bytes identity = Utf16Le(UpperAscii(user) + domain);
            Bytes key(ntowf1, ntowf1 + 16);
            return HmacMd5(key, identity);
        }

        Ntlmv2Response ComputeNtlmv2(const std::string& user, const std::string& domain, const std::string& password,
                                     const uint8_t serverChallenge[8], const uint8_t clientChallenge[8],
                                     uint64_t timestamp, const Bytes& targetInfo)
        {
            Bytes ntowf2 = NtowfV2(user, domain, password);

            // temp (MS-NLMP 3.3.2): version 1/1, reserved, the timestamp, the client challenge, reserved, the
            // target info, reserved.
            Bytes temp;
            temp.push_back(0x01);
            temp.push_back(0x01);
            PutU16(temp, 0);
            PutU32(temp, 0);
            PutU64(temp, timestamp);
            temp.insert(temp.end(), clientChallenge, clientChallenge + 8);
            PutU32(temp, 0);
            temp.insert(temp.end(), targetInfo.begin(), targetInfo.end());
            PutU32(temp, 0);

            // NTProofStr = HMAC-MD5(NTOWFv2, ServerChallenge || temp).
            Bytes proofInput(serverChallenge, serverChallenge + 8);
            proofInput.insert(proofInput.end(), temp.begin(), temp.end());
            Bytes ntProof = HmacMd5(ntowf2, proofInput);

            Ntlmv2Response result;
            result.response = ntProof;
            result.response.insert(result.response.end(), temp.begin(), temp.end());
            result.sessionBaseKey = HmacMd5(ntowf2, ntProof);
            return result;
        }

        Bytes BuildNegotiateMessage(uint32_t flags)
        {
            if (flags == 0)
            {
                flags = kNegotiateUnicode | kNegotiateRequestTarget | kNegotiateSign | kNegotiateSeal |
                        kNegotiateNtlm | kNegotiateAlwaysSign | kNegotiateExtendedSessionSecurity |
                        kNegotiateVersion | kNegotiate128 | kNegotiateKeyExchange | kNegotiate56;
            }
            Bytes out;
            out.insert(out.end(), kSignature, kSignature + 8);
            PutU32(out, 1); // NEGOTIATE
            PutU32(out, flags);
            // Empty domain and workstation fields (Len, MaxLen, Offset all zero).
            for (int i = 0; i < 4; ++i)
            {
                PutU32(out, 0);
            }
            // Version (MS-NLMP 2.2.2.10): Windows pretends to be a recent build; the bytes are informational.
            out.push_back(10); // major
            out.push_back(0);  // minor
            PutU16(out, 19041);
            out.push_back(0);
            out.push_back(0);
            out.push_back(0);
            out.push_back(15); // NTLM revision current
            return out;
        }

        bool ParseChallengeMessage(const Bytes& message, ChallengeMessage& out)
        {
            if (message.size() < 48 || std::memcmp(message.data(), kSignature, 8) != 0 || GetU32(message, 8) != 2)
            {
                return false;
            }
            std::memcpy(out.serverChallenge, message.data() + 24, 8);
            out.flags = GetU32(message, 20);
            // TargetInfoFields at offset 40: Len(2), MaxLen(2), BufferOffset(4).
            uint16_t infoLen = GetU16(message, 40);
            uint32_t infoOffset = GetU32(message, 44);
            if (infoLen > 0)
            {
                if (infoOffset > message.size() || infoOffset + infoLen > message.size())
                {
                    return false;
                }
                out.targetInfo.assign(message.begin() + infoOffset, message.begin() + infoOffset + infoLen);
                // Find the timestamp AV pair (MsvAvTimestamp, id 7) if present.
                size_t p = 0;
                while (p + 4 <= out.targetInfo.size())
                {
                    uint16_t id = GetU16(out.targetInfo, p);
                    uint16_t len = GetU16(out.targetInfo, p + 2);
                    if (id == 0)
                    {
                        break;
                    }
                    if (id == 7 && len == 8 && p + 4 + 8 <= out.targetInfo.size())
                    {
                        out.timestamp = 0;
                        for (int i = 0; i < 8; ++i)
                        {
                            out.timestamp |= static_cast<uint64_t>(out.targetInfo[p + 4 + i]) << (8 * i);
                        }
                    }
                    p += 4 + len;
                }
            }
            return true;
        }

        size_t MicOffset() { return 72; }

        Bytes BuildAuthenticateMessage(const std::string& user, const std::string& domain,
                                       const std::string& workstation, const Bytes& ntChallengeResponse,
                                       const Bytes& encryptedSessionKey, uint32_t flags)
        {
            Bytes unicodeDomain = Utf16Le(domain);
            Bytes unicodeUser = Utf16Le(user);
            Bytes unicodeWorkstation = Utf16Le(workstation);
            const Bytes lmResponse(24, 0); // NTLMv2 sends an all-zero LM response

            // Fixed header is 88 bytes: signature, type, six field descriptors, flags, version, MIC.
            const uint32_t payloadStart = 88;
            uint32_t offset = payloadStart;
            auto field = [&offset](Bytes& out, size_t length) {
                PutU16(out, static_cast<uint16_t>(length));
                PutU16(out, static_cast<uint16_t>(length));
                PutU32(out, offset);
                offset += static_cast<uint32_t>(length);
            };

            Bytes out;
            out.insert(out.end(), kSignature, kSignature + 8);
            PutU32(out, 3); // AUTHENTICATE
            field(out, lmResponse.size());
            field(out, ntChallengeResponse.size());
            field(out, unicodeDomain.size());
            field(out, unicodeUser.size());
            field(out, unicodeWorkstation.size());
            field(out, encryptedSessionKey.size());
            PutU32(out, flags);
            out.push_back(10);
            out.push_back(0);
            PutU16(out, 19041);
            out.push_back(0);
            out.push_back(0);
            out.push_back(0);
            out.push_back(15);
            out.insert(out.end(), 16, 0); // MIC, filled in later if used
            out.insert(out.end(), lmResponse.begin(), lmResponse.end());
            out.insert(out.end(), ntChallengeResponse.begin(), ntChallengeResponse.end());
            out.insert(out.end(), unicodeDomain.begin(), unicodeDomain.end());
            out.insert(out.end(), unicodeUser.begin(), unicodeUser.end());
            out.insert(out.end(), unicodeWorkstation.begin(), unicodeWorkstation.end());
            out.insert(out.end(), encryptedSessionKey.begin(), encryptedSessionKey.end());
            return out;
        }

        Bytes ComputeMic(const Bytes& exportedSessionKey, const Bytes& negotiate, const Bytes& challenge,
                         const Bytes& authenticate)
        {
            Bytes all;
            all.insert(all.end(), negotiate.begin(), negotiate.end());
            all.insert(all.end(), challenge.begin(), challenge.end());
            all.insert(all.end(), authenticate.begin(), authenticate.end());
            return HmacMd5(exportedSessionKey, all);
        }
    } // namespace Rdp
} // namespace Marconi
