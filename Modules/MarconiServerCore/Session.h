// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's connection to the server: runs the Marconi protocol over a transport, pushing the screen and
// applying the viewer's input

#pragma once

#include "Auth.h"
#include "FileService.h"
#include "ClipboardAccess.h"
#include "InputSink.h"
#include "ScreenSource.h"
#include "Settings.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <atomic>
#include <memory>
#include <chrono>
#include <climits>
#include <cstdint>
#include <mutex>
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

        private:
            bool Handshake(Wire::MessageReader& reader); // Hello/Welcome/Login/LoginResult; false if refused or lost
            void RunFrameLoop();                         // this thread: capture and send the picture
            void RunInputLoop(Wire::MessageReader& reader); // the other thread: read and apply input
            bool Send(const Wire::Bytes& framed);        // one message, serialised against other senders
            void SendFrame(const Capture& capture, bool full);
            void CheckClipboard();                       // frame loop: the computer's clipboard changed; offer it
            void HandleClipboardOffer(const Wire::ClipboardOffer& offer);
            void HandleClipboardRequest(const Wire::ClipboardRequest& request);
            void HandleClipboardData(Wire::ClipboardData&& data);
            // False if `contents` is what was last exchanged. `alsoKnown` adds another form of that content instead.
            bool RememberClipboard(const ClipboardContents& contents, bool alsoKnown = false);
            void ApplyPendingRequests();                 // frame thread: display choice and size change from the viewer
            bool CheckLayout(bool force);                // frame thread: sends Displays if the layout changed
            void SendCursor();                           // frame thread: cursor shape (when new) and position (when moved)

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
            bool m_awaitText = false;
            bool m_awaitImage = false;

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
            std::atomic<bool> m_stop{false};
        };
    } // namespace Server
} // namespace Marconi
