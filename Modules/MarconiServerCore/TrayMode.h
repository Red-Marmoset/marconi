// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon's decision of what it is: the server itself (it runs its own listener, per-user settings), or only a
// status window for the Windows service's helper (it talks to the helper over the control pipe and starts no listener,
// so it can't fight the service for the port). No UI and no operating system in here: the pipe (or a test's fake) is
// behind Connector and Link, and time comes from an injected clock, so every decision can be tested on Linux.

#pragma once

#include "ControlProtocol.h"
#include "ControlSession.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace Tray
        {
            enum class Mode
            {
                Unknown,           // nothing has been tried yet
                OwnServer,         // nothing answers on the control pipe: run the server in this process
                StatusOnly,        // a helper answers: show what it reports, run no listener
                ServiceUnreadable  // something answers but can't be talked to (another version): run no listener
            };

            // One live connection to a helper. Blocking; call from one thread at a time.
            class Link
            {
            public:
                virtual ~Link() = default;
                // The helper's current status. False (with `error`) when the connection is gone.
                virtual bool GetStatus(Control::ServerStatus& status, std::string& error) = 0;
                virtual Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) = 0;
                virtual Control::ResultCode SetPasswordHash(const std::string& passwordHash, std::string& text) = 0;
            };

            enum class ConnectOutcome
            {
                Connected,
                Absent,              // nothing is listening on the pipe
                IncompatibleVersion, // something answered but speaks another protocol version (`text` says so)
                Failed               // something answered but the conversation broke off (`text` says why)
            };

            struct ConnectResult
            {
                ConnectOutcome outcome = ConnectOutcome::Absent;
                std::unique_ptr<Link> link; // set when Connected
                std::string text;
            };

            // Opens a connection to the helper (says Hello and waits for its answer).
            class Connector
            {
            public:
                virtual ~Connector() = default;
                virtual ConnectResult Connect() = 0;
            };

            // Milliseconds from any steady starting point; the tests move it by hand.
            using Clock = std::function<int64_t()>;

            struct Timing
            {
                int64_t pollIntervalMs = 3000;
                // In StatusOnly, this many polls in a row must fail (reconnecting each time) before the tray icon
                // gives up on the helper and starts its own server, so one hiccup doesn't flip it back and forth.
                int failuresBeforeOwnServer = 3;
            };

            // What a person did in the menu, and what to tell them.
            struct ActionResult
            {
                bool ok = false;
                std::string message; // plain words for a message box; set on failure
            };

            // Everything the menu is built from.
            struct Snapshot
            {
                Mode mode = Mode::Unknown;
                Control::ServerStatus status; // StatusOnly: the helper's last report
                int failedPolls = 0;          // StatusOnly: polls that failed since the last good one
                std::string notice;           // ServiceUnreadable: why, in plain words
            };

            // The polling state machine. Call Tick() often (a timer); it polls once per interval. The first Tick
            // polls straight away, so the mode is known before anything is started.
            class TrayMode
            {
            public:
                TrayMode(Connector& connector, Clock clock, Timing timing = Timing());

                // Runs when the mode changes (never for a repeat of the same mode). The owner starts or stops its own
                // server here. Called from inside Tick and the actions, on the caller's thread.
                void SetModeChangedHandler(std::function<void(Mode from, Mode to)> handler)
                {
                    m_onModeChanged = std::move(handler);
                }
                // Runs when what the menu shows changed (a viewer came, the password was set, contact was lost or
                // came back).
                void SetStatusChangedHandler(std::function<void()> handler) { m_onStatusChanged = std::move(handler); }

                void Tick();

                const Snapshot& GetSnapshot() const { return m_snapshot; }
                Mode GetMode() const { return m_snapshot.mode; }

                // Both only work in StatusOnly; otherwise they say the service can't be reached.
                ActionResult DisconnectViewer(uint32_t viewerId);
                // `passwordHash` comes from HashPassword; the plain password never crosses the pipe.
                ActionResult SetPasswordHash(const std::string& passwordHash);

            private:
                void Poll();
                void SetMode(Mode mode);
                void StatusChanged();
                void LinkBroke();

                Connector& m_connector;
                Clock m_clock;
                Timing m_timing;
                Snapshot m_snapshot;
                std::unique_ptr<Link> m_link;
                bool m_polledOnce = false;
                int64_t m_nextPollAt = 0;
                std::function<void(Mode, Mode)> m_onModeChanged;
                std::function<void()> m_onStatusChanged;
            };

            // -- Words and menu ---------------------------------------------------------------------------------

            // What to tell the person after the helper answered a request. For NotAdministrator the text says an
            // administrator is needed, whatever the helper's own text was.
            ActionResult DescribePasswordResult(Control::ResultCode code, const std::string& text);
            ActionResult DescribeDisconnectResult(Control::ResultCode code, const std::string& text);

            enum class ItemKind
            {
                Info,        // a disabled line of status
                Separator,
                SetPassword, // "Set password..." / "Change password..."
                CopyFingerprint,
                DisconnectViewer, // `viewerId` says which
                Quit
            };

            struct MenuItem
            {
                ItemKind kind = ItemKind::Info;
                std::string label;
                uint32_t viewerId = 0;
            };

            // The menu in the two modes where the tray icon runs no server (StatusOnly and ServiceUnreadable), top to
            // bottom. The wx menu is built from this; tests and --dump-menu print its labels.
            std::vector<MenuItem> StatusOnlyMenu(const Snapshot& snapshot);

            // A short name of a mode for logs and the menu dump.
            const char* ModeName(Mode mode);
        } // namespace Tray
    } // namespace Server
} // namespace Marconi
