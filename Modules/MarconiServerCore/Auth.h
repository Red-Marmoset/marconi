// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Checking the access password, with a lockout that slows down someone trying many passwords

#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Holds the server's password hash (from HashPassword; empty means no password is set) and the recent
        // failures, shared across connections. Thread-safe.
        //
        // The lockout is kept for each address separately: wrong passwords from one address lock that address out,
        // not everyone, so someone guessing can't keep the owner from logging in from somewhere else. A second
        // limit counts wrong passwords from all addresses together; while it is passed, an address gets one try
        // instead of several before it is locked out, which slows guessing spread over many addresses without
        // ever refusing an address that has made no mistake.
        class Auth
        {
        public:
            enum class Result
            {
                Allowed,      // the password matched, or no password is set
                WrongPassword,
                LockedOut     // too many wrong tries lately; `retryAfterSeconds` says how long to wait
            };

            using Clock = std::function<std::chrono::steady_clock::time_point()>;

            explicit Auth(std::string passwordHash) : m_passwordHash(std::move(passwordHash)) {}

            bool HasPassword() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return !m_passwordHash.empty();
            }

            // Replaces the password hash while the server runs (the tray icon's Set password). Clears every
            // failure count and lockout, since the new password starts fresh.
            void SetPasswordHash(std::string passwordHash)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_passwordHash = std::move(passwordHash);
                m_addresses.clear();
                m_recentFailures.clear();
            }

            // Checks one attempt from `address` (the viewer's address as text; empty counts as one unknown
            // address). While that address is locked out it returns LockedOut without even hashing, so the lockout
            // can't be worn down by guessing fast.
            Result Check(const std::string& password, const std::string& address, int& retryAfterSeconds);
            Result Check(const std::string& password, int& retryAfterSeconds)
            {
                return Check(password, std::string(), retryAfterSeconds);
            }

            // Replaces the steady clock, for tests.
            void SetClock(Clock clock)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_clock = std::move(clock);
            }

        private:
            struct AddressState
            {
                int failures = 0;
                std::chrono::steady_clock::time_point lockedUntil; // default-constructed: not locked
                std::chrono::steady_clock::time_point lastFailure;
            };

            std::chrono::steady_clock::time_point Now() const;
            void Forget(std::chrono::steady_clock::time_point now); // drops addresses and failures too old to matter

            mutable std::mutex m_mutex;
            std::string m_passwordHash;
            Clock m_clock;
            std::map<std::string, AddressState> m_addresses;
            std::deque<std::chrono::steady_clock::time_point> m_recentFailures; // from every address, oldest first
        };
    } // namespace Server
} // namespace Marconi
