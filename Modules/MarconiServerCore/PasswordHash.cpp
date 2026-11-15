// Copyright (c) 2026 Red Marmoset Studios Ltd.
// PBKDF2-HMAC-SHA256 password hashing, on the engine's HMAC-SHA256 and random bytes

#include "PasswordHash.h"

#include <Crypto/Crypto.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr size_t kSaltBytes = 16;
            constexpr size_t kHashBytes = 32; // one SHA-256 block, so PBKDF2 needs a single iteration block

            std::string ToHex(const uint8_t* data, size_t length)
            {
                static const char* digits = "0123456789abcdef";
                std::string out;
                out.reserve(length * 2);
                for (size_t i = 0; i < length; ++i)
                {
                    out.push_back(digits[data[i] >> 4]);
                    out.push_back(digits[data[i] & 0x0F]);
                }
                return out;
            }

            bool FromHex(const std::string& hex, std::vector<uint8_t>& out)
            {
                if (hex.size() % 2 != 0)
                {
                    return false;
                }
                out.clear();
                out.reserve(hex.size() / 2);
                auto value = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                for (size_t i = 0; i < hex.size(); i += 2)
                {
                    int hi = value(hex[i]);
                    int lo = value(hex[i + 1]);
                    if (hi < 0 || lo < 0)
                    {
                        return false;
                    }
                    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
                }
                return true;
            }

            // PBKDF2-HMAC-SHA256 for a 32-byte key (one output block): U1 = HMAC(password, salt || 0x00000001),
            // Ui = HMAC(password, Ui-1), the result is U1 xor U2 xor ... xor Uc.
            std::array<uint8_t, kHashBytes> Pbkdf2(const std::string& password, const std::vector<uint8_t>& salt,
                                                   uint32_t iterations)
            {
                std::vector<uint8_t> block = salt;
                const uint8_t index[4] = {0, 0, 0, 1};
                block.insert(block.end(), index, index + 4);

                const uint8_t* key = reinterpret_cast<const uint8_t*>(password.data());
                std::array<uint8_t, kHashBytes> u{};
                Crypto::HmacSha256(key, password.size(), block.data(), block.size(), u.data());
                std::array<uint8_t, kHashBytes> result = u;
                for (uint32_t i = 1; i < iterations; ++i)
                {
                    std::array<uint8_t, kHashBytes> next{};
                    Crypto::HmacSha256(key, password.size(), u.data(), u.size(), next.data());
                    u = next;
                    for (size_t j = 0; j < kHashBytes; ++j)
                    {
                        result[j] ^= u[j];
                    }
                }
                return result;
            }

            // Splits "pbkdf2$<iterations>$<saltHex>$<hashHex>". False if it doesn't match that shape.
            bool Parse(const std::string& stored, uint32_t& iterations, std::vector<uint8_t>& salt,
                       std::vector<uint8_t>& hash)
            {
                if (stored.compare(0, 7, "pbkdf2$") != 0)
                {
                    return false;
                }
                size_t a = stored.find('$', 7);
                size_t b = a == std::string::npos ? std::string::npos : stored.find('$', a + 1);
                if (a == std::string::npos || b == std::string::npos)
                {
                    return false;
                }
                std::string iterText = stored.substr(7, a - 7);
                std::string saltHex = stored.substr(a + 1, b - a - 1);
                std::string hashHex = stored.substr(b + 1);
                if (iterText.empty() ||
                    iterText.find_first_not_of("0123456789") != std::string::npos || iterText.size() > 9)
                {
                    return false;
                }
                iterations = static_cast<uint32_t>(std::stoul(iterText));
                return iterations > 0 && FromHex(saltHex, salt) && FromHex(hashHex, hash);
            }

            // Compares two byte ranges in constant time: always looks at every byte, so timing says nothing about
            // where they first differ.
            bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t length)
            {
                uint8_t diff = 0;
                for (size_t i = 0; i < length; ++i)
                {
                    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
                }
                return diff == 0;
            }
        } // namespace

        std::string HashPassword(const std::string& password, uint32_t iterations)
        {
            std::vector<uint8_t> salt(kSaltBytes);
            if (!Crypto::RandomBytes(salt.data(), salt.size()))
            {
                return {}; // no secure randomness: the caller treats an empty result as a failure
            }
            std::array<uint8_t, kHashBytes> hash = Pbkdf2(password, salt, iterations);
            char prefix[32];
            std::snprintf(prefix, sizeof(prefix), "pbkdf2$%u$", iterations);
            return std::string(prefix) + ToHex(salt.data(), salt.size()) + "$" + ToHex(hash.data(), hash.size());
        }

        bool HashMatches(const std::string& stored, const std::string& password)
        {
            uint32_t iterations = 0;
            std::vector<uint8_t> salt;
            std::vector<uint8_t> hash;
            if (!Parse(stored, iterations, salt, hash) || hash.size() != kHashBytes)
            {
                return false;
            }
            std::array<uint8_t, kHashBytes> computed = Pbkdf2(password, salt, iterations);
            return ConstantTimeEqual(computed.data(), hash.data(), kHashBytes);
        }
    } // namespace Server
} // namespace Marconi
