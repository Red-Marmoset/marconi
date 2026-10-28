// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Connecting the way the phone and tablet apps do: straight from the typed address, without the probe the desktop
// viewer runs first. "host" and "vnc://" addresses get the VNC client, "marconi://" the Marconi protocol, "ssh://"
// VNC through the computer's SSH server.

#pragma once

#include <MarconiCore/Address.h>
#include <MarconiCore/RemoteSession.h>
#include <MarconiCore/Rfb/Client.h>
#include <MarconiCore/Wire/WireClient.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    struct QuickConnectOptions
    {
        std::string address;  // as typed: "studio-pc", "192.168.1.20:1", "vnc://host::5901", "ssh://user@host"
        std::string username; // used in place of any user name in the address when not empty
        std::string password; // sent first; the listener is asked when it's missing or refused
        std::vector<uint8_t> trustedCertificateSha256; // a certificate accepted before (see OnUntrustedCertificate)
        std::string clientName = "Marconi";             // what a Marconi server shows in its log
    };

    // Starts connecting on a worker thread and returns the session, whose picture, input and callbacks work as
    // RemoteSession describes. Returns nothing, with `error` set, when the address can't be used: it doesn't parse,
    // or it names Remote Desktop, which these apps don't have yet. A connection that fails later ends in the
    // listener's OnDisconnected.
    std::unique_ptr<RemoteSession> QuickConnect(const QuickConnectOptions& options, SessionListener& listener,
                                                std::string& error);

    // The options each client gets for an address, for tests.
    Rfb::ConnectionOptions QuickConnectVncOptions(const Address& address, const QuickConnectOptions& options);
    Wire::ClientOptions QuickConnectMarconiOptions(const Address& address, const QuickConnectOptions& options);
} // namespace Marconi
