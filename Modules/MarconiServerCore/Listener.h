// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Accepts viewer connections on the Marconi port and serves each one a Session

#pragma once

#include "InputSink.h"
#include "ScreenSource.h"
#include "Session.h"
#include "Settings.h"

#include <MarconiCore/Net/Socket.h>

#include <atomic>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // Listens on the settings' port and serves one viewer at a time (Phase 1; several at once comes later).
        // The screen and input are shared across connections, since there is one of each on this computer.
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

        private:
            ScreenSource& m_screen;
            InputSink& m_input;
            Settings m_settings;
            Net::Socket m_listenSocket;
            uint16_t m_port = 0;
            int m_frameIntervalMs = 16;
            std::atomic<bool> m_stop{false};
            std::atomic<Session*> m_current{nullptr}; // the live session, so Stop() can end it
        };
    } // namespace Server
} // namespace Marconi
