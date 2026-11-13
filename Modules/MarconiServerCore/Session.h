// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's connection to the server: runs the Marconi protocol over a transport, pushing the screen and
// applying the viewer's input

#pragma once

#include "Auth.h"
#include "ClipboardAccess.h"
#include "InputSink.h"
#include "ScreenSource.h"
#include "Settings.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <atomic>
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

            Wire::Transport& m_transport;
            ScreenSource& m_screen;
            InputSink& m_input;
            Settings m_settings;
            Auth* m_auth = nullptr;
            uint64_t m_clientCapabilities = 0; // what the viewer offered in Hello
            uint64_t m_frameId = 0;
            int m_frameIntervalMs = 16;
            std::mutex m_writeMutex;

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
            std::atomic<bool> m_stop{false};
        };
    } // namespace Server
} // namespace Marconi
