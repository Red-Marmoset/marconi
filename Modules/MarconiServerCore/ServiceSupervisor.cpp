// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows service's decision logic; see ServiceSupervisor.h

#include "ServiceSupervisor.h"

#include <Log/Log.h>

namespace Marconi
{
    namespace Server
    {
        ServiceSupervisor::ServiceSupervisor(ConsoleSessionSource& sessions, HelperLauncher& launcher,
                                             const SupervisorPolicy& policy)
            : m_sessions(sessions), m_launcher(launcher), m_policy(policy)
        {
        }

        ServiceSupervisor::~ServiceSupervisor()
        {
            Stop();
        }

        int64_t ServiceSupervisor::RetryDelayMs() const
        {
            // 1st failure: firstRetryMs, 2nd: double, and so on up to the ceiling.
            int64_t delay = m_policy.firstRetryMs;
            for (int i = 1; i < m_failures && delay < m_policy.maxRetryMs; ++i)
            {
                delay *= 2;
            }
            return delay < m_policy.maxRetryMs ? delay : m_policy.maxRetryMs;
        }

        void ServiceSupervisor::EndHelper()
        {
            if (m_handle != 0)
            {
                m_launcher.Terminate(m_handle);
                m_handle = 0;
            }
        }

        void ServiceSupervisor::Stop()
        {
            m_stopped = true;
            EndHelper();
        }

        void ServiceSupervisor::Tick(int64_t nowMs)
        {
            if (m_stopped)
            {
                return;
            }
            uint32_t session = m_sessions.GetActiveConsoleSessionId();

            // A different console session from last time: whatever was waiting out a failure no longer applies, so
            // the new session is tried at once.
            if (session != m_lastSeenSession)
            {
                m_lastSeenSession = session;
                m_failures = 0;
                m_nextStartMs = nowMs;
            }

            if (m_handle != 0)
            {
                if (session != m_helperSession)
                {
                    LOG_INFO("Service", "Console session changed from %u to %u: ending the helper",
                             static_cast<unsigned>(m_helperSession), static_cast<unsigned>(session));
                    EndHelper();
                }
                else if (!m_launcher.IsRunning(m_handle))
                {
                    // It exited on its own (crash, quit, killed). A helper that had been up a while starts the
                    // backoff over; one that dies at once makes the wait grow.
                    bool wasStable = nowMs - m_startedAtMs >= m_policy.stableAfterMs;
                    EndHelper(); // releases the handle of the exited process
                    if (wasStable)
                    {
                        m_failures = 0;
                    }
                    ++m_failures;
                    m_nextStartMs = nowMs + RetryDelayMs();
                    LOG_INFO("Service", "The helper for session %u exited; starting it again in %lld ms",
                             static_cast<unsigned>(m_helperSession), static_cast<long long>(RetryDelayMs()));
                }
            }

            if (m_handle == 0 && session != kNoConsoleSession && nowMs >= m_nextStartMs)
            {
                HelperHandle handle = 0;
                std::string error;
                if (m_launcher.Launch(session, handle, error) && handle != 0)
                {
                    m_handle = handle;
                    m_helperSession = session;
                    m_startedAtMs = nowMs;
                    m_lastError.clear();
                }
                else
                {
                    ++m_failures;
                    m_nextStartMs = nowMs + RetryDelayMs();
                    m_lastError = error.empty() ? "the helper could not be started" : error;
                    LOG_INFO("Service", "Could not start the helper in session %u: %s", static_cast<unsigned>(session),
                             m_lastError.c_str());
                }
            }
        }
    } // namespace Server
} // namespace Marconi
