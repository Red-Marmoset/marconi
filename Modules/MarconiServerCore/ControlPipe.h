// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The local named pipe between the session helper (server) and the tray icon (client). The messages and the state
// machine are in ControlSession; this is the Windows pipe under them. Other platforms have no pipe yet: Start and
// Connect fail with a message.

#pragma once

#include "ControlSession.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            // The helper's end: listens on the pipe, and runs a ControlSession for each tray icon that connects (one
            // thread each). The pipe is open to SYSTEM and administrators fully and to signed-in users for reading
            // and writing, and refuses connections from other computers. Whether a client is an administrator is
            // worked out from the connection (the client's token), not from anything it sends.
            class ControlPipeServer
            {
            public:
                ControlPipeServer(ControlHost& host, const std::string& serverVersion);
                ~ControlPipeServer();

                ControlPipeServer(const ControlPipeServer&) = delete;
                ControlPipeServer& operator=(const ControlPipeServer&) = delete;

                bool Start(std::string& error);
                void Stop();

                // Tells every connected tray icon the status changed (a viewer came or went).
                void NotifyChanged();

            private:
                struct Impl;
                std::unique_ptr<Impl> m_impl;
            };

            // The tray icon's end: connects to the helper's pipe. Null (with `error`) when the helper isn't
            // running, so the tray icon can fall back to running the server itself.
            std::unique_ptr<Wire::Transport> ConnectToControlPipe(std::string& error);
        } // namespace Control
    } // namespace Server
} // namespace Marconi
