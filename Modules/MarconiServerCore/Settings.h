// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the server was configured with: the port it listens on and the name it shows viewers

#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

            // Let viewers in when no password is set. Off by default: a server with no password refuses every login
            // (the viewer is told so) until one is set. Only for testing and for people who choose it knowingly.
            bool allowNoPassword = false;

            // Lock this computer's screen whenever a viewer that has logged in disconnects (closes the window, loses
            // the connection or says Bye), whether or not the viewer asked. Off by default. A viewer can still ask
            // for a lock itself with the Lock message.
            bool lockOnDisconnect = false;

            // The server's TLS identity, as PEM text: the certificate it presents and its private key. Every
            // connection is encrypted with it, and the client pins its fingerprint on first use. Empty means the
            // listener makes a fresh self-signed identity when it starts (its fingerprint then changes each run, so
            // a lasting server saves and reloads one).
            std::string certificatePem;
            std::string privateKeyPem;

            // Whether viewers may browse and copy this computer's files (needs the viewer to offer file transfer
            // too). Off by default: letting a viewer reach the computer's files is a bigger step than letting it see
            // the screen, so it is turned on deliberately (the tray menu, or --files).
            bool filesEnabled = false;
            // The one folder viewers may reach ("/" to them), or empty for the whole computer.
            std::string fileRoot;

            // Who may connect: addresses ("192.168.1.20", "::1") and networks ("192.168.1.0/24", "fd00::/8"). Empty
            // means anyone who can reach the port, which is how the server behaved before there was a list. The
            // listener refuses to start if an entry can't be read, rather than quietly letting everyone in.
            std::vector<std::string> allowedAddresses;

            // End a viewer's session after this many minutes with nothing sent from it (no mouse, key, clipboard or
            // file activity). 0 = never.
            int idleMinutes = 0;
        };
    } // namespace Server
} // namespace Marconi
