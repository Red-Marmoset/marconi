// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NTLMv2, the authentication RDP's Network Level Authentication (CredSSP) carries. This builds the three NTLM
// messages and computes the NTLMv2 response, on the engine's Md4 / HmacMd5 / Rc4. See Documentation/RDP.md and
// MS-NLMP. Marconi is only ever the client, and only does NTLMv2 (not the older LM / NTLMv1).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // NTOWFv2 = HMAC-MD5(MD4(UTF16LE(password)), UTF16LE(uppercase(user) + domain)). The 16-byte key every
        // other NTLMv2 value derives from. The domain is not uppercased; an empty domain is allowed.
        Bytes NtowfV2(const std::string& user, const std::string& domain, const std::string& password);

        struct Ntlmv2Response
        {
            Bytes response;       // NTProofStr (16 bytes) followed by the blob: the NtChallengeResponse sent
            Bytes sessionBaseKey; // 16 bytes: HMAC-MD5(NTOWFv2, NTProofStr), the root of the session keys
        };

        // The NTLMv2 response. `serverChallenge` is 8 bytes (from the Challenge message), `clientChallenge` 8 bytes
        // (random; fixed in tests), `timestamp` the Windows FILETIME (100 ns ticks since 1601), `targetInfo` the
        // AV-pair blob copied from the Challenge. UTF-16/ASCII inputs are as typed.
        Ntlmv2Response ComputeNtlmv2(const std::string& user, const std::string& domain, const std::string& password,
                                     const uint8_t serverChallenge[8], const uint8_t clientChallenge[8],
                                     uint64_t timestamp, const Bytes& targetInfo);

        // NEGOTIATE (type 1): the first message, advertising what the client can do. `flags` are the NTLMSSP
        // negotiate flags; Marconi's default set is used when 0.
        Bytes BuildNegotiateMessage(uint32_t flags = 0);

        struct ChallengeMessage
        {
            uint8_t serverChallenge[8] = {};
            uint32_t flags = 0;
            Bytes targetInfo;    // the AV-pair blob, copied into the response
            uint64_t timestamp = 0; // from the target info's timestamp AV pair, or 0 if absent
        };

        // Parses a CHALLENGE (type 2). False if the bytes aren't a well-formed challenge.
        bool ParseChallengeMessage(const Bytes& message, ChallengeMessage& out);

        // AUTHENTICATE (type 3): carries the NTLMv2 response and the (here empty) LM response, the domain, user and
        // workstation, and the encrypted session key. `encryptedSessionKey` may be empty when key exchange is off.
        Bytes BuildAuthenticateMessage(const std::string& user, const std::string& domain,
                                       const std::string& workstation, const Bytes& ntChallengeResponse,
                                       const Bytes& encryptedSessionKey, uint32_t flags);

        // The MIC (message integrity check) over the three messages, written into the AUTHENTICATE message at the
        // offset BuildAuthenticateMessage reserved. Exposed for the CredSSP layer, which knows all three messages.
        Bytes ComputeMic(const Bytes& exportedSessionKey, const Bytes& negotiate, const Bytes& challenge,
                         const Bytes& authenticate);
        // Where the 16-byte MIC sits inside an AUTHENTICATE message built here (so it can be filled in later).
        size_t MicOffset();
    } // namespace Rdp
} // namespace Marconi
