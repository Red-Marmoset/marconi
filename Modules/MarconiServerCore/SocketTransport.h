// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A Wire transport over a plain TCP socket, for the loopback skeleton (the TLS stream replaces it once encryption
// is added)

#pragma once

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Wire/WireChannel.h>

namespace Marconi
{
    namespace Server
    {
        // Carries the Marconi protocol straight over a connected socket. The session reads on one thread and writes
        // on another; Socket allows that. Shutting the socket down (from the listener, when it stops) unblocks both.
        class SocketTransport : public Wire::Transport
        {
        public:
            explicit SocketTransport(Net::Socket& socket) : m_socket(socket) {}

            ptrdiff_t Read(void* buffer, size_t length) override { return m_socket.Receive(buffer, length); }
            bool Write(const void* data, size_t length) override { return m_socket.SendAll(data, length); }

        private:
            Net::Socket& m_socket;
        };
    } // namespace Server
} // namespace Marconi
