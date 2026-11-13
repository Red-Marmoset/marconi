// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Runs the Marconi server on a background thread for the tray app: picks the screen and input to use, starts the
// listener, and restarts it when the port or password changes

#pragma once

#include <MarconiServerCore/ClipboardAccess.h>
#include <MarconiServerCore/ConnectionNotifier.h>
#include <MarconiServerCore/InputSink.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/ScreenSource.h>
#include <MarconiServerCore/Settings.h>
#include <MarconiServerCore/TestPatternScreen.h>
#include <MarconiServerCore/UpdateHost.h>
#include <MarconiServerCore/VirtualGamepad.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

            // Why the screen can't be shared, or can't be controlled, in plain words for the person at this computer
            // (for example a missing permission on a Mac, or a Wayland desktop on Linux). Empty when all is well.
            std::string GetProblem() const;

            // Looks again for this computer's screen and input (after the person has allowed them in the system's
            // settings) and restarts the listener on whatever it finds. False (with `error`) if it can't listen.
            bool CheckScreenAndInputAgain(std::string& error);

            // Changes the port and restarts the listener on it. False (with `error`) if the new port can't be opened;
            // the old listener is left stopped either way.
            bool ChangePort(uint16_t port, std::string& error);

            // Lets viewers browse and copy this computer's files, or stops them (restarts the listener).
            bool ChangeFilesEnabled(bool enabled, std::string& error);

            // Changes the access password (already hashed) and restarts so logins are checked against it.
            bool ChangePasswordHash(const std::string& passwordHash, std::string& error);

            // Who may connect (Settings::allowedAddresses), applied at once to the running server without a restart.
            // False (with `error`, in plain words) if an entry can't be read; nothing then changes.
            bool ChangeAllowedAddresses(const std::vector<std::string>& entries, std::string& error);

            // Disconnect a viewer after this many minutes of no input (0 = never), also for the one connected now.
            void ChangeIdleMinutes(int minutes);

            // Where connection notices go (null = nowhere). It must outlive the controller. Call before Start.
            void SetNotifier(Server::ConnectionNotifier* notifier);

            // Lets a newer Marconi update this server (SERVER_PLAN.md, "Self-update"): the installer is kept in
            // `stagingFolder`, `approver` asks the person at this computer (it must outlive the controller), and
            // `onInstallerStarted` runs on a server thread once the installer was started, so the program can quit.
            // Where this system can't update itself (everywhere but Windows), viewers are told so. Call before Start.
            void SetUpdateHost(const std::string& stagingFolder, Server::UpdateApprover* approver,
                               std::function<void()> onInstallerStarted);
            // Changes "Let a newer Marconi update this server" and "allow unsigned updates" (restarts the listener).
            bool ChangeUpdateSettings(Server::UpdatePermission permission, bool allowUnsigned, std::string& error);

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
            std::unique_ptr<Server::ClipboardAccess> m_clipboard; // shared only with the real screen
            std::unique_ptr<Server::VirtualGamepadBackend> m_gamepads; // makes viewers' controllers; real screen only
            Server::ScreenSource* m_screen = nullptr;
            Server::InputSink* m_input = nullptr;
            std::string m_servedDescription;
            std::string m_problem;
            Server::ConnectionNotifier* m_notifier = nullptr;

            std::unique_ptr<Server::InstallerInspector> m_updateInspector;
            std::unique_ptr<Server::InstallerLauncher> m_updateLauncher;
            Server::UpdateEnvironment m_updateEnvironment; // given to every listener; launcher null = not supported

            std::unique_ptr<Server::Listener> m_listener;
            std::thread m_thread;
            bool m_running = false;
        };
    } // namespace ServerUI
} // namespace Marconi
