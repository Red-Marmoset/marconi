// Copyright (c) 2026 Red Marmoset Studios Ltd.
// HKDF-SHA256, AES-256-GCM (mbedTLS) and the replay window for the datagram channel

#include <MarconiCore/Wire/DatagramCrypto.h>

#include <Crypto/Crypto.h>

#include <mbedtls/gcm.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace Marconi
{
    namespace Wire
    {
        Bytes HkdfSha256(const Bytes& salt, const Bytes& inputKeyMaterial, const Bytes& info, size_t length)
        {
            uint8_t pseudoRandomKey[32];
            // Extract: PRK = HMAC(salt, IKM). An empty salt means 32 zero bytes (RFC 5869, 2.2).
            Bytes zeroSalt(32, 0);
            const Bytes& usedSalt = salt.empty() ? zeroSalt : salt;
            Crypto::HmacSha256(usedSalt.data(), usedSalt.size(), inputKeyMaterial.data(), inputKeyMaterial.size(),
                               pseudoRandomKey);
            // Expand: T(i) = HMAC(PRK, T(i-1) | info | i)
            Bytes output;
            Bytes previous;
            for (uint8_t counter = 1; output.size() < length && counter != 0; ++counter)
            {
                Bytes block = previous;
                block.insert(block.end(), info.begin(), info.end());
                block.push_back(counter);
                uint8_t mac[32];
                Crypto::HmacSha256(pseudoRandomKey, sizeof(pseudoRandomKey), block.data(), block.size(), mac);
                previous.assign(mac, mac + 32);
                output.insert(output.end(), previous.begin(), previous.end());
            }
            output.resize(std::min(output.size(), length));
            return output;
        }

        namespace
        {
            Bytes ToBytes(const std::string& text)
            {
                return Bytes(text.begin(), text.end());
            }

            void Fill(const Bytes& secret, const Bytes& salt, const char* label, DirectionKeys& out)
            {
                Bytes key = HkdfSha256(salt, secret, ToBytes(std::string("marconi udp ") + label + " key"),
                                       kDatagramKeyBytes);
                Bytes iv =
                    HkdfSha256(salt, secret, ToBytes(std::string("marconi udp ") + label + " iv"), kDatagramIvBytes);
                std::copy(key.begin(), key.end(), out.key.begin());
                std::copy(iv.begin(), iv.end(), out.iv.begin());
            }

            void MakeNonce(const std::array<uint8_t, kDatagramIvBytes>& iv, uint64_t packetNumber, uint8_t nonce[12])
            {
                std::memcpy(nonce, iv.data(), kDatagramIvBytes);
                for (int i = 0; i < 8; ++i)
                {
                    nonce[4 + i] ^= static_cast<uint8_t>(packetNumber >> (56 - 8 * i));
                }
            }
        } // namespace

        DatagramKeys DeriveDatagramKeys(const Bytes& secret, uint32_t sessionId)
        {
            Writer salt;
            salt.Raw("marconi datagram v1", 19);
            salt.U32(sessionId);
            DatagramKeys keys;
            Fill(secret, salt.Data(), "c2s", keys.clientToServer);
            Fill(secret, salt.Data(), "s2c", keys.serverToClient);
            return keys;
        }

        struct DatagramAead::Impl
        {
            mbedtls_gcm_context context;
            Impl() { mbedtls_gcm_init(&context); }
            ~Impl() { mbedtls_gcm_free(&context); }
        };

        DatagramAead::DatagramAead(const DirectionKeys& keys) : m_impl(new Impl), m_iv(keys.iv)
        {
            m_valid = mbedtls_gcm_setkey(&m_impl->context, MBEDTLS_CIPHER_ID_AES, keys.key.data(),
                                         static_cast<unsigned int>(kDatagramKeyBytes * 8)) == 0;
        }

        DatagramAead::~DatagramAead() = default;

        bool DatagramAead::Seal(uint64_t packetNumber, const uint8_t* aad, size_t aadSize, const uint8_t* plain,
                                size_t plainSize, uint8_t* out)
        {
            if (!m_valid)
            {
                return false;
            }
            uint8_t nonce[12];
            MakeNonce(m_iv, packetNumber, nonce);
            return mbedtls_gcm_crypt_and_tag(&m_impl->context, MBEDTLS_GCM_ENCRYPT, plainSize, nonce, sizeof(nonce),
                                             aad, aadSize, plain, out, kDatagramTagBytes, out + plainSize) == 0;
        }

        bool DatagramAead::Open(uint64_t packetNumber, const uint8_t* aad, size_t aadSize, const uint8_t* in,
                                size_t inSize, uint8_t* out)
        {
            if (!m_valid || inSize < kDatagramTagBytes)
            {
                return false;
            }
            uint8_t nonce[12];
            MakeNonce(m_iv, packetNumber, nonce);
            size_t cipherSize = inSize - kDatagramTagBytes;
            return mbedtls_gcm_auth_decrypt(&m_impl->context, cipherSize, nonce, sizeof(nonce), aad, aadSize,
                                            in + cipherSize, kDatagramTagBytes, in, out) == 0;
        }

        bool ReplayWindow::WouldAccept(uint64_t packetNumber) const
        {
            if (!m_any || packetNumber > m_highest)
            {
                return true;
            }
            uint64_t behind = m_highest - packetNumber;
            if (behind >= kSize)
            {
                return false;
            }
            return (m_bits[behind / 64] & (1ull << (behind % 64))) == 0;
        }

        bool ReplayWindow::Mark(uint64_t packetNumber)
        {
            if (!WouldAccept(packetNumber))
            {
                return false;
            }
            if (!m_any)
            {
                m_any = true;
                m_highest = packetNumber;
                m_bits.fill(0);
                m_bits[0] = 1;
                return true;
            }
            if (packetNumber > m_highest)
            {
                uint64_t shift = packetNumber - m_highest;
                if (shift >= kSize)
                {
                    m_bits.fill(0);
                }
                else
                {
                    // Shift the bitmap left by `shift` bits (bit i = packet highest - i).
                    uint64_t wordShift = shift / 64;
                    uint64_t bitShift = shift % 64;
                    for (int i = static_cast<int>(m_bits.size()) - 1; i >= 0; --i)
                    {
                        uint64_t value = 0;
                        if (static_cast<uint64_t>(i) >= wordShift)
                        {
                            value = m_bits[i - wordShift] << bitShift;
                            if (bitShift != 0 && static_cast<uint64_t>(i) > wordShift)
                            {
                                value |= m_bits[i - wordShift - 1] >> (64 - bitShift);
                            }
                        }
                        m_bits[i] = value;
                    }
                }
                m_bits[0] |= 1;
                m_highest = packetNumber;
                return true;
            }
            uint64_t behind = m_highest - packetNumber;
            m_bits[behind / 64] |= 1ull << (behind % 64);
            return true;
        }
    } // namespace Wire
} // namespace Marconi
