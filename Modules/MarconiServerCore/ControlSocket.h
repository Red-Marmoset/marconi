// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The control connection over a Unix domain socket, for macOS and Linux. There is no service on those systems yet, so
// this is for developers and tests: it lets a real Listener with a ListenerControlHost and the tray icon's logic talk
// to each other without Windows (the console server's --control-socket and the tray's --control-socket options). The
// messages and the state machine are ControlSession's, exactly as over the Windows pipe. Windows has no Unix socket
// code here; these functions say so.

#pragma once

#include "ControlSession.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            // Who counts as an administrator on the socket. The real pipe decides from the client's token; here there
            // is nothing like it, so a developer picks.
            enum class SocketAdmins
            {
                Nobody,    // every peer is refused Set password (to try the refusal text)
                Everybody, // every peer may set the password
                RootOnly   // peers running as root (user 0)
            };

            class ControlSocketServer
            {
            public:
                ControlSocketServer(ControlHost& host, const std::string& serverVersion,
                                    SocketAdmins admins = SocketAdmins::RootOnly);
                ~ControlSocketServer();

                ControlSocketServer(const ControlSocketServer&) = delete;
                ControlSocketServer& operator=(const ControlSocketServer&) = delete;

                // Listens on a socket file at `path` (an older file there is replaced; only this user can open it).
                bool Start(const std::string& path, std::string& error);
                void Stop();

                // Tells every connected tray icon the status changed.
                void NotifyChanged();

            private:
                struct Impl;
                std::unique_ptr<Impl> m_impl;
            };

            // Connects to a ControlSocketServer. Null (with `error`) when nothing is listening at `path`.
            std::unique_ptr<Wire::Transport> ConnectToControlSocket(const std::string& path, std::string& error);
        } // namespace Control
    } // namespace Server
} // namespace Marconi
