// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Accepts viewer connections on the Marconi port and serves each one a Session

#pragma once

#include "AllowList.h"
#include "Auth.h"
#include "ClipboardAccess.h"
#include "ConnectionNotifier.h"
#include "IdleWatch.h"
#include "InputSink.h"
#include "ScreenSource.h"
#include "Session.h"
#include "Settings.h"

#include <MarconiCore/Net/Socket.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // Listens on the settings' port and serves one viewer at a time (Phase 1; several at once comes later).
        // The screen and input are shared across connections, since there is one of each on this computer.
        // Who is connected, for the tray icon.
        struct ViewerSnapshot
        {
            uint32_t id = 0;
            std::string name;
            uint32_t connectedSeconds = 0;
        };

        class Listener
        {
        public:
            Listener(ScreenSource& screen, InputSink& input, const Settings& settings);

            Listener(const Listener&) = delete;
            Listener& operator=(const Listener&) = delete;

            // Binds and starts listening. False (with `error`) if the port is taken or can't be opened. `bindAddress`
            // defaults to every interface; tests pass 127.0.0.1.
            bool Start(std::string& error, const std::string& bindAddress = "0.0.0.0");
            // The port actually bound (useful when the settings' port was 0, which picks a free one).
            uint16_t GetPort() const { return m_port; }

            // Accepts one viewer (waiting up to `acceptTimeoutMs`, or for ever if 0) and serves it until it
            // disconnects. Returns false if nothing connected in time or the listener was stopped.
            bool ServeOne(int acceptTimeoutMs = 0);

            // Accepts and serves viewers one after another until Stop().
            void Run();

            // Stops accepting and ends the current viewer's session. Safe to call from another thread.
            void Stop();

            void SetFrameIntervalMs(int ms) { m_frameIntervalMs = ms; }

            // Shares this computer's clipboard with each viewer (null, the default, shares none). The clipboard
            // must outlive the listener. Call before Run() or ServeOne().
            void SetClipboard(ClipboardAccess* clipboard) { m_clipboard = clipboard; }

            // Lets each viewer's game controllers appear on this computer, made by `backend` (null, the default, offers
            // none). The backend must outlive the listener. Call before Run() or ServeOne().
            void SetGamepads(VirtualGamepadBackend* backend) { m_gamepadBackend = backend; }

            // The SHA-256 fingerprint of the certificate the server presents, as a lowercase hex string, so it can
            // be shown to the person setting the server up. Empty before Start() or if the identity is bad.
            const std::string& GetCertificateFingerprint() const { return m_fingerprint; }

            // The viewer that is connected and logged in, if any (one at a time for now). False when nobody is.
            bool GetViewer(ViewerSnapshot& viewer) const;

            // Changes the password logins are checked against while the server runs ("" = none).
            void SetPasswordHash(const std::string& passwordHash) { m_auth.SetPasswordHash(passwordHash); }

            // Who may connect (see Settings::allowedAddresses). Takes effect for the next connection; a viewer already
            // connected is not ended by it. False (with `error`) if an entry can't be read, leaving the list as it was.
            bool SetAllowedAddresses(const std::vector<std::string>& entries, std::string& error);

            // Tells `notifier` (null = nobody; it must outlive the listener) when a viewer connects or goes away.
            // Call before Run() or ServeOne().
            void SetNotifier(ConnectionNotifier* notifier) { m_notifier = notifier; }

            // The idle limit in minutes (0 = never), also for a viewer that is connected now.
            void SetIdleMinutes(int minutes);
            // The clock the idle limit is measured with; tests move it by hand. Call before Run() or ServeOne().
            void SetIdleClock(IdleClock clock) { m_idleClock = std::move(clock); }

            // Ends the viewer with this id (from GetViewer), or the connected one when `viewerId` is 0. False if
            // there was none to end.
            bool DisconnectViewer(uint32_t viewerId);

        private:
            // Makes sure there is a usable TLS identity (generating a self-signed one if the settings have none)
            // and records its fingerprint. False (with `error`) if generation failed.
            bool EnsureIdentity(std::string& error);

            ScreenSource& m_screen;
            InputSink& m_input;
            Settings m_settings;
            Auth m_auth; // checks the password; shared across viewers so the lockout covers the whole server
            Net::Socket m_listenSocket;
            uint16_t m_port = 0;
            std::string m_fingerprint; // the certificate's SHA-256, in hex, for display
            int m_frameIntervalMs = 16;
            ClipboardAccess* m_clipboard = nullptr;
            VirtualGamepadBackend* m_gamepadBackend = nullptr;
            ConnectionNotifier* m_notifier = nullptr;
            IdleClock m_idleClock;
            std::atomic<int> m_idleMinutes{0};
            mutable std::mutex m_allowMutex;
            AllowList m_allow; // guarded by m_allowMutex
            std::atomic<bool> m_stop{false};
            mutable std::mutex m_currentMutex; // guards the members below
            Session* m_current = nullptr;      // the live session, so Stop() and DisconnectViewer can end it
            Net::Socket* m_currentSocket = nullptr; // the live session's connection, shut down to end it
            uint32_t m_currentId = 0;
            uint32_t m_viewerCounter = 0;
            std::chrono::steady_clock::time_point m_currentSince;
        };
    } // namespace Server
} // namespace Marconi
