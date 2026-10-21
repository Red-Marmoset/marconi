// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the server was configured with: the port it listens on and the name it shows viewers

#pragma once

#include <cstdint>
#include <string>

#include <MarconiCore/Wire/WireMessages.h>

namespace Marconi
{
    namespace Server
    {
        struct Settings
        {
            uint16_t port = Wire::kDefaultPort;
            std::string serverName;    // the computer's name, shown in the viewer's title bar; empty = the host name
            std::string serverVersion; // Marconi Server's version string, for the Welcome message

            // The access password, hashed by HashPassword (empty = no password set, so any viewer is let in). The
            // viewer sends the plain password at login and the server checks it against this, inside the encrypted
            // connection.
            std::string passwordHash;

            // The server's TLS identity, as PEM text: the certificate it presents and its private key. Every
            // connection is encrypted with it, and the client pins its fingerprint on first use. Empty means the
            // listener makes a fresh self-signed identity when it starts (its fingerprint then changes each run, so
            // a lasting server saves and reloads one).
            std::string certificatePem;
            std::string privateKeyPem;
        };
    } // namespace Server
} // namespace Marconi
