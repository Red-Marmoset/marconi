// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NTLM message protection (MS-NLMP 3.4): the signing and sealing keys derived from the session key, and the
// seal/unseal that CredSSP uses on its public-key proof and credentials. Extended session security with key
// exchange, which is what RDP negotiates.

#pragma once

#include <MarconiCore/Rdp/Rc4State.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // MS-NLMP 3.4.5.2 / 3.4.5.3: MD5 of the session key and a magic string (its terminating NUL included). The
        // four magics below name the two directions and the two purposes.
        Bytes DeriveNtlmKey(const Bytes& exportedSessionKey, const std::string& magic);

        extern const char* const kClientSigningMagic;
        extern const char* const kServerSigningMagic;
        extern const char* const kClientSealingMagic;
        extern const char* const kServerSealingMagic;

        // One direction's protection: a signing key and a continuous RC4 keystream, with a climbing sequence number.
        class NtlmProtection
        {
        public:
            NtlmProtection(const Bytes& signingKey, const Bytes& sealingKey);

            // The 16-byte signature followed by the sealed data (EncryptMessage, MS-NLMP 3.4.4.2).
            Bytes Seal(const Bytes& data);
            // Strips the signature, unseals, and verifies. `ok` is false if the signature doesn't match.
            Bytes Unseal(const Bytes& message, bool& ok);

        private:
            Bytes m_signingKey;
            Rc4State m_rc4;
            uint32_t m_sequence = 0;
        };
    } // namespace Rdp
} // namespace Marconi
