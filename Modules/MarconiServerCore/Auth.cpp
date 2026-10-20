// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The password check and its lockout

#include "Auth.h"

#include "PasswordHash.h"

#include <algorithm>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // Allow a few wrong tries, then lock for a time that doubles with each further failure, up to a cap.
            constexpr int kFailuresBeforeLockout = 5;
            constexpr int kBaseLockoutSeconds = 5;
            constexpr int kMaxLockoutSeconds = 5 * 60;
        } // namespace

        Auth::Result Auth::Check(const std::string& password, int& retryAfterSeconds)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            retryAfterSeconds = 0;

            // No password set: the server lets anyone in (a server with nothing to protect). The program warns
            // about this at startup; setting a password turns the check on.
            if (m_passwordHash.empty())
            {
                return Result::Allowed;
            }

            auto now = std::chrono::steady_clock::now();
            if (now < m_lockedUntil)
            {
                retryAfterSeconds = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(m_lockedUntil - now).count() + 1);
                return Result::LockedOut;
            }

            if (HashMatches(m_passwordHash, password))
            {
                m_failures = 0;
                return Result::Allowed;
            }

            ++m_failures;
            if (m_failures >= kFailuresBeforeLockout)
            {
                int over = m_failures - kFailuresBeforeLockout; // 0 on the first lockout
                long long seconds = static_cast<long long>(kBaseLockoutSeconds) << std::min(over, 20);
                int lockout = static_cast<int>(std::min<long long>(seconds, kMaxLockoutSeconds));
                m_lockedUntil = now + std::chrono::seconds(lockout);
                retryAfterSeconds = lockout;
                return Result::LockedOut;
            }
            return Result::WrongPassword;
        }
    } // namespace Server
} // namespace Marconi
