// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encrypted (TLS) connection over a Socket: the factory, until the engine's Crypto module provides TLS

#include <MarconiCore/Net/Tls.h>

namespace Marconi
{
    namespace Net
    {
        // Everything else (VeNCrypt negotiation, switching the connection to the encrypted stream, certificate
        // prompts, the status bar) is in place; this is the one piece still missing. It belongs with the engine's
        // Crypto module, which wraps each platform's own crypto library: Schannel on Windows, Security.framework on
        // macOS, OpenSSL on Linux. Tools/TlsStream_OpenSSL.cpp is a working reference implementation of this
        // interface, tested against a TLS server; port it to each platform's library and return it here.
        bool IsTlsAvailable()
        {
            return false;
        }

        std::unique_ptr<TlsStream> CreateTlsStream(Socket&, const TlsOptions&)
        {
            return nullptr;
        }
    } // namespace Net
} // namespace Marconi
