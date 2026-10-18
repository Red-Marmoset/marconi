// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Running the Marconi protocol's framed messages over a two-way byte transport (a socket, a TLS stream, or an
// in-memory pipe in tests)

#pragma once

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Wire/WireStream.h>

#include <cstddef>

namespace Marconi
{
    namespace Wire
    {
        // The transport the control channel runs over. One implementation wraps a TCP socket, another the TLS
        // stream, another an in-memory pipe for tests. Read blocks until at least one byte arrives and returns the
        // count, 0 at end of stream, -1 on error; Write blocks until everything is sent, false on error. Read and
        // Write may run on different threads at once (the server sends frames while it reads input).
        class Transport
        {
        public:
            virtual ~Transport() = default;
            virtual ptrdiff_t Read(void* buffer, size_t length) = 0;
            virtual bool Write(const void* data, size_t length) = 0;
        };

        // Reads whole framed messages off a transport, one at a time. A frame is a varint length then that many
        // bytes (the type byte and the body). Next() returns the body (type byte included, length stripped), ready
        // for Reader + ReadType. It buffers across reads, so a message split over several packets still arrives
        // whole. Throws WireError at end of stream, on a transport error, or if a length exceeds kMaxMessageBytes.
        class MessageReader
        {
        public:
            explicit MessageReader(Transport& transport) : m_transport(transport) {}

            Bytes Next();

        private:
            uint8_t ReadByte();              // one byte, buffered; throws at end of stream
            void ReadExactly(uint8_t* out, size_t length);

            Transport& m_transport;
            Bytes m_buffer;      // bytes read from the transport but not yet consumed
            size_t m_position = 0;
        };

        // Writes an already-framed message (from a message's Encode()) to the transport. False if the transport
        // failed.
        bool WriteMessage(Transport& transport, const Bytes& framed);

        // The protocol straight over a connected TCP socket (the loopback skeleton, before encryption). The TLS
        // stream will be another Transport once it exists. The socket is owned by the caller; reading and writing
        // from two threads is allowed, and shutting the socket down unblocks both.
        class SocketTransport : public Transport
        {
        public:
            explicit SocketTransport(Net::Socket& socket) : m_socket(socket) {}
            ptrdiff_t Read(void* buffer, size_t length) override { return m_socket.Receive(buffer, length); }
            bool Write(const void* data, size_t length) override { return m_socket.SendAll(data, length); }

        private:
            Net::Socket& m_socket;
        };
    } // namespace Wire
} // namespace Marconi
