// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Runs the Marconi server on a background thread for the tray app: picks the screen and input to use, starts the
// listener, and restarts it when the port or password changes

#pragma once

#include <MarconiServerCore/InputSink.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/ScreenSource.h>
#include <MarconiServerCore/Settings.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace ServerUI
    {
        // Owns a running server. Start picks this computer's screen and input where they exist (Windows for now),
        // falling back to a moving test pattern with a logging input sink, then listens on a background thread.
        // The port and password can be changed while it runs; each change restarts the listener.
        class ServerController
        {
        public:
            explicit ServerController(const Server::Settings& settings);
            ~ServerController();

            ServerController(const ServerController&) = delete;
            ServerController& operator=(const ServerController&) = delete;

            // Starts listening. False (with `error`) if the port can't be opened. Safe to call once; use Restart to
            // apply later changes.
            bool Start(std::string& error);

            // Stops the listener and joins its thread. Safe to call when already stopped.
            void Stop();

            bool IsRunning() const;

            // The port actually bound (the settings' port, or a free one when that was 0).
            uint16_t GetBoundPort() const;

            // The certificate fingerprint viewers are asked to trust, as lowercase hex. Stable across restarts
            // because the identity is kept in the settings.
            std::string GetFingerprint() const;

            // A plain phrase for what the server is sending: "this computer's screen" or "a test pattern".
            std::string GetServedDescription() const;

            // Changes the port and restarts the listener on it. False (with `error`) if the new port can't be opened;
            // the old listener is left stopped either way.
            bool ChangePort(uint16_t port, std::string& error);

            // Changes the access password (already hashed; empty means no password) and restarts so logins are
            // checked against it.
            bool ChangePasswordHash(const std::string& passwordHash, std::string& error);

            const Server::Settings& GetSettings() const { return m_settings; }

        private:
            // Picks the screen and input once, the first time the server starts.
            void ChooseScreenAndInput();
            bool StartLocked(std::string& error);
            void StopLocked();

            mutable std::mutex m_mutex;
            Server::Settings m_settings;

            std::unique_ptr<Server::ScreenSource> m_systemScreen;
            std::unique_ptr<Server::InputSink> m_systemInput;
            std::unique_ptr<Server::TestPatternScreen> m_pattern;
            std::unique_ptr<Server::InputSink> m_loggingInput;
            Server::ScreenSource* m_screen = nullptr;
            Server::InputSink* m_input = nullptr;
            std::string m_servedDescription;

            std::unique_ptr<Server::Listener> m_listener;
            std::thread m_thread;
            bool m_running = false;
        };
    } // namespace ServerUI
} // namespace Marconi
