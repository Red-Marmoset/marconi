// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Checking the access password, with a lockout that slows down someone trying many passwords

#pragma once

#include <chrono>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Holds the server's password hash (from HashPassword; empty means no password is set) and the recent
        // failure count, shared across connections so the lockout applies to the whole server. Thread-safe.
        class Auth
        {
        public:
            enum class Result
            {
                Allowed,      // the password matched, or no password is set
                WrongPassword,
                LockedOut     // too many wrong tries lately; `retryAfterSeconds` says how long to wait
            };

            explicit Auth(std::string passwordHash) : m_passwordHash(std::move(passwordHash)) {}

            bool HasPassword() const { return !m_passwordHash.empty(); }

            // Checks one attempt. On too many recent failures it returns LockedOut without even hashing, so the
            // lockout can't be worn down by guessing fast.
            Result Check(const std::string& password, int& retryAfterSeconds);

        private:
            std::mutex m_mutex;
            std::string m_passwordHash;
            int m_failures = 0;
            std::chrono::steady_clock::time_point m_lockedUntil; // default-constructed: not locked
        };
    } // namespace Server
} // namespace Marconi
