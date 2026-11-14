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
            // An address's count is forgotten once it has made no mistake for this long.
            constexpr auto kForgetAfter = std::chrono::minutes(30);
            // Wrong passwords from all addresses together: more than this many inside the window, and an address
            // is locked out at its first mistake instead of its fifth.
            constexpr size_t kBusyFailures = 20;
            constexpr auto kBusyWindow = std::chrono::minutes(10);
            // The most addresses remembered at once. Past it the oldest are dropped; an attacker using that many
            // addresses is already held to the "busy" rule above.
            constexpr size_t kMaxAddresses = 4096;
        } // namespace

        std::chrono::steady_clock::time_point Auth::Now() const
        {
            return m_clock ? m_clock() : std::chrono::steady_clock::now();
        }

        void Auth::Forget(std::chrono::steady_clock::time_point now)
        {
            while (!m_recentFailures.empty() && now - m_recentFailures.front() > kBusyWindow)
            {
                m_recentFailures.pop_front();
            }
            for (auto it = m_addresses.begin(); it != m_addresses.end();)
            {
                if (now >= it->second.lockedUntil && now - it->second.lastFailure > kForgetAfter)
                {
                    it = m_addresses.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            while (m_addresses.size() > kMaxAddresses)
            {
                auto oldest = std::min_element(m_addresses.begin(), m_addresses.end(), [](const auto& a, const auto& b) {
                    return a.second.lastFailure < b.second.lastFailure;
                });
                m_addresses.erase(oldest);
            }
        }

        Auth::Result Auth::Check(const std::string& password, const std::string& address, int& retryAfterSeconds)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            retryAfterSeconds = 0;

            // No password set: the server lets anyone in (a server with nothing to protect). The program warns
            // about this at startup; setting a password turns the check on.
            if (m_passwordHash.empty())
            {
                return Result::Allowed;
            }

            const auto now = Now();
            Forget(now);
            auto found = m_addresses.find(address);
            if (found != m_addresses.end() && now < found->second.lockedUntil)
            {
                retryAfterSeconds = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(found->second.lockedUntil - now).count() + 1);
                return Result::LockedOut;
            }

            if (HashMatches(m_passwordHash, password))
            {
                if (found != m_addresses.end())
                {
                    m_addresses.erase(found);
                }
                return Result::Allowed;
            }

            AddressState& state = m_addresses[address];
            ++state.failures;
            state.lastFailure = now;
            m_recentFailures.push_back(now);
            // With wrong passwords arriving from many addresses, each address gets one try, not five.
            const bool busy = m_recentFailures.size() > kBusyFailures;
            const int allowed = busy ? 1 : kFailuresBeforeLockout;
            if (state.failures >= allowed)
            {
                int over = state.failures - allowed; // 0 on the first lockout
                long long seconds = static_cast<long long>(kBaseLockoutSeconds) << std::min(over, 20);
                int lockout = static_cast<int>(std::min<long long>(seconds, kMaxLockoutSeconds));
                state.lockedUntil = now + std::chrono::seconds(lockout);
                retryAfterSeconds = lockout;
                return Result::LockedOut;
            }
            return Result::WrongPassword;
        }
    } // namespace Server
} // namespace Marconi
