// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Hashing the server's access password so the plain password is never stored: PBKDF2-HMAC-SHA256 with a random
// salt (the engine's Crypto provides HMAC-SHA256 and the random bytes)

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Hashes `password` with a fresh random salt and returns a self-describing string that HashMatches reads
        // back: "pbkdf2$<iterations>$<saltHex>$<hashHex>". Store this, never the password.
        std::string HashPassword(const std::string& password, uint32_t iterations = 200000);

        // True if `password` hashes to the same thing as `stored` (a string from HashPassword). The comparison is
        // constant-time, so a wrong password can't be found one character at a time by timing. False if `stored`
        // isn't a hash this code wrote.
        bool HashMatches(const std::string& stored, const std::string& password);
    } // namespace Server
} // namespace Marconi
