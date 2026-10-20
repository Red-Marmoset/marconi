// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A Wire transport over the engine's TLS server stream, so the session runs encrypted

#pragma once

#include <Crypto/TlsServer.h>
#include <MarconiCore/Wire/WireChannel.h>

#include <memory>
#include <utility>

namespace Marconi
{
    namespace Server
    {
        // Carries the Marconi protocol over a Crypto::TlsServer that has already finished its handshake. Owns the
        // stream. Reading and writing from two threads is allowed, as the stream serialises its own use.
        class TlsServerTransport : public Wire::Transport
        {
        public:
            explicit TlsServerTransport(std::unique_ptr<Crypto::TlsServer> tls) : m_tls(std::move(tls)) {}

            ptrdiff_t Read(void* buffer, size_t length) override { return m_tls->Read(buffer, length); }
            bool Write(const void* data, size_t length) override { return m_tls->Write(data, length); }

            Crypto::TlsServer& Stream() { return *m_tls; }

        private:
            std::unique_ptr<Crypto::TlsServer> m_tls;
        };
    } // namespace Server
} // namespace Marconi
