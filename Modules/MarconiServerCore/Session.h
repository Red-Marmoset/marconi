// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's connection to the server: runs the Marconi protocol over a transport, pushing the screen and
// applying the viewer's input

#pragma once

#include "Auth.h"
#include "FileService.h"
#include "ClipboardAccess.h"
#include "ConnectionNotifier.h"
#include "IdleWatch.h"
#include "InputSink.h"
#include "ScreenSource.h"
#include "Settings.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <atomic>
#include <memory>
#include <chrono>
#include <climits>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

            // Shares this computer's clipboard with the viewer (text and images, both ways). Null, the default,
            // leaves clipboard sharing off: the Welcome then doesn't grant it. Call before Run().
            void SetClipboard(ClipboardAccess* clipboard) { m_clipboard = clipboard; }
            // Tells `notifier` (null = nobody) when this viewer connects (after the login) and when it goes away.
            // `address` is where it connected from, for the notice. Call before Run().
            void SetNotifier(ConnectionNotifier* notifier, const std::string& address)
            {
                m_notifier = notifier;
                m_peerAddress = address;
            }
            // Ends the session when the viewer sends nothing for the settings' idleMinutes (0 = never). The limit can
            // be changed while it runs; `clock` replaces the steady clock, for tests.
            void SetIdleMinutes(int minutes) { m_idle.SetLimitMinutes(minutes); }
            void SetIdleClock(IdleClock clock) { m_idle.SetClock(std::move(clock)); }
            // Called when the server ends the session itself (for being idle) so the owner of the connection can
            // shut it down and wake the thread waiting to read from the viewer. Call before Run().
            void SetEndHandler(std::function<void()> handler) { m_endHandler = std::move(handler); }
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
            void StopClipboardHandler();                 // stops listening for clipboard changes
            void CheckClipboard();                       // frame loop: the computer's clipboard changed; offer it
            void HandleClipboardOffer(const Wire::ClipboardOffer& offer);
            void HandleClipboardRequest(const Wire::ClipboardRequest& request);
            void HandleClipboardData(Wire::ClipboardData&& data);
            // False if `contents` is what was last exchanged. `alsoKnown` adds another form of that content instead.
            bool RememberClipboard(const ClipboardContents& contents, bool alsoKnown = false);
            void ApplyPendingRequests();                 // frame thread: display choice and size change from the viewer
            bool CheckLayout(bool force);                // frame thread: sends Displays if the layout changed
            void SendCursor();                           // frame thread: cursor shape (when new) and position (when moved)
            void EndIdleViewer();                        // frame thread: says why, then ends the session
            void LockComputer(); // locks the screen through the input sink, once per viewer

            Wire::Transport& m_transport;
            ScreenSource& m_screen;
            InputSink& m_input;
            Settings m_settings;
            Auth* m_auth = nullptr;
            uint64_t m_clientCapabilities = 0; // what the viewer offered in Hello
            uint64_t m_grantedCapabilities = 0; // what this connection uses
            uint64_t m_frameId = 0;
            int m_frameIntervalMs = 16;
            std::mutex m_writeMutex;
            std::unique_ptr<FileService> m_files; // answers the viewer's file messages

            // Clipboard sharing. `m_clipboardActive` is set once the viewer was granted it. The change handler only
            // raises `m_clipboardChanged`; the frame loop does the reading. `m_recentHashes` holds hashes of what
            // was last offered to the viewer or written from it, so the change our own write causes isn't offered
            // back (which would loop). The rest tracks the formats still awaited from the viewer's last offer.
            ClipboardAccess* m_clipboard = nullptr;
            bool m_clipboardActive = false;
            std::atomic<bool> m_clipboardChanged{false};
            std::mutex m_clipboardMutex;
            std::vector<uint64_t> m_recentHashes;
            ClipboardContents m_incoming;
            int m_awaitText = 0;  // requests sent for the viewer's text that it hasn't answered yet
            int m_awaitImage = 0; // the same for its image (rapid copies leave several outstanding)

            // The viewer's display requests, set on the input thread and applied on the frame thread (the screen
            // source is only called from the frame thread).
            std::mutex m_requestMutex;
            bool m_selectionPending = false;
            uint32_t m_requestedDisplay = 0;
            bool m_modePending = false;
            Wire::SetDisplayMode m_requestedMode;
            // Frame thread only.
            std::vector<Wire::Display> m_displays; // the layout last sent
            uint32_t m_selectedDisplay = 0;        // 0 = the whole desktop
            bool m_needFull = false;               // the next frame must be a full picture of the selection
            int32_t m_lastCursorX = INT32_MIN;
            int32_t m_lastCursorY = INT32_MIN;
            std::chrono::steady_clock::time_point m_lastLayoutCheck;
            IdleWatch m_idle;
            ConnectionNotifier* m_notifier = nullptr;
            std::string m_peerAddress;
            std::function<void()> m_endHandler;
            std::atomic<bool> m_endedIdle{false};
            std::atomic<bool> m_stop{false};
            std::atomic<bool> m_stoppedByServer{false}; // Stop() was called, as opposed to the viewer leaving
            std::atomic<bool> m_loggedIn{false};
            std::atomic<bool> m_locked{false};
            mutable std::mutex m_nameMutex;
            std::string m_viewerName; // guarded by m_nameMutex
        };
    } // namespace Server
} // namespace Marconi
