// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encrypted (TLS) connection over a Socket: the interface Marconi needs, and the factory that provides it

#pragma once

#include <MarconiCore/Net/Socket.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        // The certificate a server presented when this computer doesn't trust it, so the user can decide.
        struct TlsCertificate
        {
            std::vector<uint8_t> sha256; // fingerprint, 32 bytes
            std::string subject;         // who it was issued to, e.g. "CN=studio-mac.local"
            std::string issuer;          // who issued it; the same as the subject when self-signed
            std::string validUntil;      // expiry, in words
            std::string problem;         // why it isn't trusted: self-signed, name doesn't match, expired...
        };

        struct TlsOptions
        {
            std::string hostName;   // what the certificate's name is checked against
            bool anonymous = false; // the server presents no certificate (VeNCrypt's TLS* subtypes)
            // Asked, on the thread running Handshake(), when the certificate isn't trusted by this computer. Return
            // true to connect anyway. Unset: untrusted certificates are refused.
            std::function<bool(const TlsCertificate&)> trustDecision;
        };

        // An encrypted connection on top of a connected Socket. Implementations wrap the platform's TLS library.
        class TlsStream
        {
        public:
            virtual ~TlsStream() = default;

            // Runs the TLS handshake. False (with `error`) if it failed or the certificate was refused.
            virtual bool Handshake(std::string& error) = 0;

            // As Socket::Receive and Socket::SendAll, on the encrypted stream: Read blocks until at least one byte
            // of application data arrives and returns 0 at end of stream (including after the socket is shut down),
            // -1 on error; Write blocks until everything is sent. Read and Write may be called at the same time from
            // two threads (the connection thread reads while the UI thread sends input), so implementations must
            // serialise their use of the TLS library without making a writer wait for data to arrive.
            virtual ptrdiff_t Read(void* buffer, size_t length) = 0;
            virtual bool Write(const void* data, size_t length) = 0;

            // Sends the TLS close notice, best effort.
            virtual void Shutdown() = 0;

            // What the connection is encrypted with, for the status bar: e.g. "TLS 1.3".
            virtual std::string GetProtocolName() const = 0;
        };

        // False in a build without a TLS implementation; CreateTlsStream then returns nullptr and Marconi never
        // chooses an encrypted security type.
        bool IsTlsAvailable();
        // Whether `anonymous` connections (no certificate) can be made: some TLS libraries have no such suites.
        bool IsAnonymousTlsAvailable();
        std::unique_ptr<TlsStream> CreateTlsStream(Socket& socket, const TlsOptions& options);
    } // namespace Net
} // namespace Marconi
