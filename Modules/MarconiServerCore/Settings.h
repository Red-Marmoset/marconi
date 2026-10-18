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

            // The access password is deliberately not here yet: it is checked inside TLS, which arrives with the
            // password handling in the next step. Until then a session accepts any viewer on the loopback skeleton.
        };
    } // namespace Server
} // namespace Marconi
