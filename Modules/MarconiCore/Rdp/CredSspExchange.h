// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The CredSSP exchange (MS-CSSP 3.1.5): drives the NTLM login over the already-open TLS channel, binding it to the
// server's public key so a man in the middle can't relay it. This is the one RDP piece that can't be checked on a
// loopback server here; it follows the specification and is confirmed against a real server. See Documentation/RDP.md.

#pragma once

#include <MarconiCore/Rdp/Security.h>

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Rdp
    {
        struct CredSspCredentials
        {
            std::string domain;
            std::string user;
            std::string password;
        };

        // Runs the four-leg CredSSP exchange over `tls` (a SecurityLink carrying the TLS application data).
        // `serverPublicKey` is the TLS peer's SubjectPublicKeyInfo (Crypto::TlsClient::GetPeerPublicKey). On failure
        // returns false with `error`; `errorCode` carries the server's NTSTATUS when it reported one (e.g. a wrong
        // password gives 0xC000006D / 0xC0000022). On success the server has accepted the login.
        bool RunCredSsp(const SecurityLink& tls, const Bytes& serverPublicKey, const CredSspCredentials& credentials,
                        uint32_t& errorCode, std::string& error);
    } // namespace Rdp
} // namespace Marconi
