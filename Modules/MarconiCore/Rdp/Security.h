// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP milestone 1: the security negotiation over a byte link, before any TLS upgrade. See Documentation/RDP.md.

#pragma once

#include <MarconiCore/Rdp/Negotiation.h>

#include <cstddef>
#include <functional>
#include <string>

namespace Marconi
{
    namespace Rdp
    {
        // The bytes under the negotiation: a socket's, or (in tests) an in-memory pair. Read as Socket::Receive
        // (>0 bytes, 0 at end, -1 on error), SendAll blocks until everything is sent.
        struct SecurityLink
        {
            std::function<ptrdiff_t(void* buffer, size_t length)> receive;
            std::function<bool(const void* data, size_t length)> sendAll;
        };

        // Sends the Connection Request for `requestedProtocols` and reads the one TPKT frame of the Connection
        // Confirm. False (with `error`) on a broken connection or a malformed reply; true with `out` filled in for
        // a well-formed reply, including one that reports a negotiation failure (check out.failed).
        bool NegotiateSecurity(const SecurityLink& link, uint32_t requestedProtocols, const std::string& cookie,
                               NegotiationResponse& out, std::string& error);
    } // namespace Rdp
} // namespace Marconi
