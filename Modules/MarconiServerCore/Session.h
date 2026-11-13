// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's connection to the server: runs the Marconi protocol over a transport, pushing the screen and
// applying the viewer's input

#pragma once

#include "Auth.h"
#include "InputSink.h"
#include "ScreenSource.h"
#include "Settings.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        // Drives one connection. Given a transport already connected to a viewer (a plain socket on the loopback
        // skeleton; the TLS stream once encryption is added), it runs the handshake, then pushes the screen as it
        // changes while a second thread reads the viewer's input and hands it to the InputSink. Run() blocks until
        // the viewer disconnects or Stop() is called; the caller shuts the transport down to unblock a thread
        // waiting on the network.
        class Session
        {
        public:
            // `auth` checks the viewer's password; null lets any viewer in (the loopback skeleton and tests that
            // don't exercise the password). It is shared across connections so the lockout covers the whole server.
            Session(Wire::Transport& transport, ScreenSource& screen, InputSink& input, const Settings& settings,
                    Auth* auth = nullptr);

            Session(const Session&) = delete;
            Session& operator=(const Session&) = delete;

            void Run();
            void Stop();

            // How often the frame loop grabs the screen, in milliseconds. The default is about 60 a second; tests
            // set their own. A grab with nothing changed sends no message.
            void SetFrameIntervalMs(int ms) { m_frameIntervalMs = ms; }

            // The viewer's name from its Hello (empty before that), for the tray icon's status. Thread-safe.
            std::string GetViewerName() const;
            // True once the viewer has passed the login.
            bool IsLoggedIn() const { return m_loggedIn; }

        private:
            bool Handshake(Wire::MessageReader& reader); // Hello/Welcome/Login/LoginResult; false if refused or lost
            void RunFrameLoop();                         // this thread: capture and send the picture
            void RunInputLoop(Wire::MessageReader& reader); // the other thread: read and apply input
            bool Send(const Wire::Bytes& framed);        // one message, serialised against other senders
            void SendFrame(const Capture& capture, bool full);
            void LockComputer(); // locks the screen through the input sink, once per viewer

            Wire::Transport& m_transport;
            ScreenSource& m_screen;
            InputSink& m_input;
            Settings m_settings;
            Auth* m_auth = nullptr;
            uint64_t m_clientCapabilities = 0; // what the viewer offered in Hello
            uint64_t m_frameId = 0;
            int m_frameIntervalMs = 16;
            std::mutex m_writeMutex;
            std::atomic<bool> m_stop{false};
            std::atomic<bool> m_stoppedByServer{false}; // Stop() was called, as opposed to the viewer leaving
            std::atomic<bool> m_loggedIn{false};
            std::atomic<bool> m_locked{false};
            mutable std::mutex m_nameMutex;
            std::string m_viewerName; // guarded by m_nameMutex
        };
    } // namespace Server
} // namespace Marconi
