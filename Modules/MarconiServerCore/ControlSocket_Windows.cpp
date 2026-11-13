// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No Unix socket control connection on Windows (the named pipe in ControlPipe_Windows.cpp is the real one); these
// only say so, so code that offers the developer option still links. NOT YET BUILT ON WINDOWS: it was written on
// Linux and is a few lines of plain C++, but nobody has compiled it there.

#include "ControlSocket.h"

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            struct ControlSocketServer::Impl
            {
            };

            ControlSocketServer::ControlSocketServer(ControlHost&, const std::string&, SocketAdmins) : m_impl(new Impl)
            {
            }

            ControlSocketServer::~ControlSocketServer() = default;

            bool ControlSocketServer::Start(const std::string&, std::string& error)
            {
                error = "the control socket is for macOS and Linux only";
                return false;
            }

            void ControlSocketServer::Stop()
            {
            }

            void ControlSocketServer::NotifyChanged()
            {
            }

            std::unique_ptr<Wire::Transport> ConnectToControlSocket(const std::string&, std::string& error)
            {
                error = "the control socket is for macOS and Linux only";
                return nullptr;
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
