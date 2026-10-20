// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NTLM message protection (MS-NLMP 3.4): key derivation and the seal/unseal CredSSP uses.

#include <MarconiCore/Rdp/NtlmSeal.h>

#include <Crypto/Crypto.h>

#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        const char* const kClientSigningMagic = "session key to client-to-server signing key magic constant";
        const char* const kServerSigningMagic = "session key to server-to-client signing key magic constant";
        const char* const kClientSealingMagic = "session key to client-to-server sealing key magic constant";
        const char* const kServerSealingMagic = "session key to server-to-client sealing key magic constant";

        namespace
        {
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }
        } // namespace

        Bytes DeriveNtlmKey(const Bytes& exportedSessionKey, const std::string& magic)
        {
            Bytes input = exportedSessionKey;
            input.insert(input.end(), magic.begin(), magic.end());
            input.push_back(0x00); // the magic string's terminating NUL is part of the input
            Bytes out(16);
            Crypto::Md5(input.data(), input.size(), out.data());
            return out;
        }

        NtlmProtection::NtlmProtection(const Bytes& signingKey, const Bytes& sealingKey)
            : m_signingKey(signingKey), m_rc4(sealingKey)
        {
        }

        Bytes NtlmProtection::Seal(const Bytes& data)
        {
            Bytes sealed = m_rc4.Process(data);
            Bytes checksumInput;
            PutU32(checksumInput, m_sequence);
            checksumInput.insert(checksumInput.end(), data.begin(), data.end());
            Bytes mac(16);
            Crypto::HmacMd5(m_signingKey.data(), m_signingKey.size(), checksumInput.data(), checksumInput.size(),
                            mac.data());
            Bytes checksum(mac.begin(), mac.begin() + 8);
            Bytes sealedChecksum = m_rc4.Process(checksum); // key exchange: the checksum rides the same keystream
            Bytes out;
            PutU32(out, 1); // version
            out.insert(out.end(), sealedChecksum.begin(), sealedChecksum.end());
            PutU32(out, m_sequence);
            out.insert(out.end(), sealed.begin(), sealed.end());
            ++m_sequence;
            return out;
        }

        Bytes NtlmProtection::Unseal(const Bytes& message, bool& ok)
        {
            ok = false;
            if (message.size() < 16)
            {
                return {};
            }
            Bytes data = m_rc4.Process(Bytes(message.begin() + 16, message.end()));
            Bytes checksumInput;
            PutU32(checksumInput, m_sequence);
            checksumInput.insert(checksumInput.end(), data.begin(), data.end());
            Bytes mac(16);
            Crypto::HmacMd5(m_signingKey.data(), m_signingKey.size(), checksumInput.data(), checksumInput.size(),
                            mac.data());
            Bytes checksum(mac.begin(), mac.begin() + 8);
            Bytes sealedChecksum = m_rc4.Process(checksum);
            ok = std::memcmp(message.data() + 4, sealedChecksum.data(), 8) == 0;
            ++m_sequence;
            return data;
        }
    } // namespace Rdp
} // namespace Marconi
