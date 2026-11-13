// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Both ends of the control pipe: ControlSession is the helper's side (answers one tray icon), ControlClient is the
// tray icon's. They run over any Wire::Transport, so the tests use an in-memory one and Windows uses a named pipe.

#pragma once

#include "ControlProtocol.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <atomic>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            // What the helper does when the tray icon asks. The helper implements it over its listener and its
            // settings; tests use a fake. Called from the ControlSession's thread.
            class ControlHost
            {
            public:
                virtual ~ControlHost() = default;
                virtual ServerStatus GetStatus() = 0;
                // Ends one viewer (id from the status) or every viewer (0). NoSuchViewer if none matched.
                virtual ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) = 0;
                // Stores the new password hash and makes it the one logins are checked against. The session has
                // already checked the caller is an administrator.
                virtual ResultCode SetPasswordHash(const std::string& passwordHash, std::string& text) = 0;
            };

            // Facts about the other end that the pipe itself vouches for, not anything the peer says. The Windows
            // pipe fills `isAdministrator` by impersonating the connected client and checking its token, so a tray
            // icon can't claim to be an administrator.
            struct PeerInfo
            {
                bool isAdministrator = false;
            };

            // The helper's side of one control connection. State: waiting for Hello, then ready. In "ready" each
            // request gets exactly one answer (Status for GetStatus, Result for the rest); a message of an unknown
            // type is skipped, a malformed one or one before Hello ends the connection.
            class ControlSession
            {
            public:
                ControlSession(Wire::Transport& transport, ControlHost& host, const PeerInfo& peer,
                               const std::string& serverVersion);

                ControlSession(const ControlSession&) = delete;
                ControlSession& operator=(const ControlSession&) = delete;

                // Reads and answers messages until the other end goes away, sends nonsense or Stop is called (the
                // owner also shuts the transport down so a blocked read returns).
                void Run();
                void Stop() { m_stop = true; }

                // Pushes a Status to the tray icon (when the helper's state changed). False if the pipe broke or the
                // tray icon hasn't said Hello yet. Safe to call from any thread.
                bool PushStatus();

                bool IsReady() const { return m_ready; }

            private:
                bool Send(const Wire::Bytes& framed);
                void Reply(Type request, ResultCode code, const std::string& text);
                bool Handle(Wire::Reader& body, Type type); // false ends the connection

                Wire::Transport& m_transport;
                ControlHost& m_host;
                PeerInfo m_peer;
                std::string m_serverVersion;
                std::mutex m_writeMutex;
                std::atomic<bool> m_ready{false}; // the Hello arrived and was accepted
                std::atomic<bool> m_stop{false};
            };

            // The tray icon's side. Everything is a blocking request and answer, so call it from a worker thread,
            // one request at a time. A Status the helper pushes between requests is kept as the latest one.
            class ControlClient
            {
            public:
                explicit ControlClient(Wire::Transport& transport) : m_transport(transport), m_reader(transport) {}

                // Says Hello, waits for Welcome and the first Status. False (with `error`) on a refusal, a version
                // the helper doesn't speak, or a broken pipe.
                bool Connect(const std::string& clientName, std::string& error);

                // Asks for the current status.
                bool RefreshStatus(ServerStatus& status, std::string& error);

                // Ends a viewer's connection (0 = all). `text` carries the helper's reason when not Ok.
                ResultCode DisconnectViewer(uint32_t viewerId, std::string& text);

                // Sends a new password hash. NotAdministrator means the caller must run elevated and try again.
                ResultCode SetPasswordHash(const std::string& passwordHash, std::string& text);

                const ServerStatus& GetLatestStatus() const { return m_latest; }
                const std::string& GetServerVersion() const { return m_serverVersion; }

                // True when the last Connect failed because the helper speaks a different protocol version (its
                // refusal, or a Welcome with another version), as opposed to a broken pipe.
                bool WasVersionMismatch() const { return m_versionMismatch; }

            private:
                bool Send(const Wire::Bytes& framed);
                // Reads until a message of `want` arrives, taking pushed Status messages on the way. Throws nothing;
                // false with `error` when the pipe ends first.
                bool WaitFor(Type want, Wire::Bytes& bytes, std::string& error);
                ResultCode Request(const Wire::Bytes& message, Type requestType, std::string& text);

                Wire::Transport& m_transport;
                Wire::MessageReader m_reader;
                ServerStatus m_latest;
                std::string m_serverVersion;
                bool m_versionMismatch = false;
                bool m_refused = false; // a Result arrived where something else was wanted
            };
        } // namespace Control
    } // namespace Server
} // namespace Marconi
