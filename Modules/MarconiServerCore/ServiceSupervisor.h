// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows service's decision logic: which console session the helper process should be running in, and when to
// start, stop or restart it. No Windows calls here: the session list and the process launcher are interfaces, so
// the logic is tested on any platform with fakes. The real ones are in ServiceHost_Windows.cpp.

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // What WTSGetActiveConsoleSessionId returns while no session is attached to the console (during a fast user
        // switch or a remote desktop handover). The supervisor treats it as "nothing to serve right now".
        constexpr uint32_t kNoConsoleSession = 0xFFFFFFFFu;

        // Which session is on the computer's own screen and keyboard.
        class ConsoleSessionSource
        {
        public:
            virtual ~ConsoleSessionSource() = default;
            virtual uint32_t GetActiveConsoleSessionId() = 0;
        };

        // Starts and watches the helper process (`MarconiServer.exe --session <id>`). A handle is whatever the
        // launcher uses to find the process again; 0 is never a valid handle.
        using HelperHandle = uint64_t;

        class HelperLauncher
        {
        public:
            virtual ~HelperLauncher() = default;
            // Starts the helper in `sessionId` as SYSTEM. False (with `error`) if it couldn't start.
            virtual bool Launch(uint32_t sessionId, HelperHandle& handle, std::string& error) = 0;
            // True while the process is still running.
            virtual bool IsRunning(HelperHandle handle) = 0;
            // Ends the process if it is still running (asking it to stop first, then forcing it) and releases the
            // handle. Safe on a process that already exited.
            virtual void Terminate(HelperHandle handle) = 0;
        };

        // How quickly a helper that keeps dying is restarted. The wait doubles after each failure up to a ceiling,
        // and resets once a helper has stayed up for `stableAfterMs`, so a crash loop doesn't spin the machine and a
        // one-off crash is back within a second.
        struct SupervisorPolicy
        {
            int64_t firstRetryMs = 1000;
            int64_t maxRetryMs = 30000;
            int64_t stableAfterMs = 60000;
        };

        // Keeps one helper running in the active console session. Call Tick often (the service does once a second
        // and again whenever Windows reports a session change); every call looks at the session and the helper and
        // acts. `nowMs` is any steadily increasing millisecond clock, so tests drive time themselves.
        //
        //  - The console session changes (fast user switching, remote desktop took the console): the helper in the
        //    old session is ended and one is started in the new session straight away.
        //  - No console session: the helper is ended and nothing starts until a session appears.
        //  - The helper exits or fails to start: it is started again after a growing wait.
        class ServiceSupervisor
        {
        public:
            ServiceSupervisor(ConsoleSessionSource& sessions, HelperLauncher& launcher,
                              const SupervisorPolicy& policy = SupervisorPolicy());
            ~ServiceSupervisor();

            ServiceSupervisor(const ServiceSupervisor&) = delete;
            ServiceSupervisor& operator=(const ServiceSupervisor&) = delete;

            void Tick(int64_t nowMs);

            // Ends the helper (the service is stopping). Tick starts nothing after this.
            void Stop();

            bool IsHelperRunning() const { return m_handle != 0; }
            // The session the running helper serves, or kNoConsoleSession when none is running.
            uint32_t GetHelperSession() const { return m_handle != 0 ? m_helperSession : kNoConsoleSession; }
            // Failed or short-lived starts in a row (0 once a helper has run long enough).
            int GetFailureCount() const { return m_failures; }
            // The last launch error, for the service log; empty when the last launch worked.
            const std::string& GetLastError() const { return m_lastError; }

        private:
            int64_t RetryDelayMs() const;
            void EndHelper();

            ConsoleSessionSource& m_sessions;
            HelperLauncher& m_launcher;
            SupervisorPolicy m_policy;

            bool m_stopped = false;
            HelperHandle m_handle = 0;
            uint32_t m_helperSession = kNoConsoleSession; // the session the running helper was started for
            uint32_t m_lastSeenSession = kNoConsoleSession;
            int64_t m_startedAtMs = 0;
            int64_t m_nextStartMs = 0;
            int m_failures = 0;
            std::string m_lastError;
        };
    } // namespace Server
} // namespace Marconi
