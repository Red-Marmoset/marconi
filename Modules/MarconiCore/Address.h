// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Parsing of user-typed addresses ("host", "user@host", "host:display", "host::port", "rdp://host", "[ipv6]:display")

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    enum class Protocol
    {
        Auto,    // not chosen: probe to find out
        Marconi, // Marconi's own server and protocol
        Vnc,
        Rdp,
        Ssh,
    };

    struct Address
    {
        std::string host;
        uint16_t port = 0;    // 0 = the protocol's usual port (all of them for Auto)
        std::string username = {}; // from "user@host": log in with that account
        Protocol protocol = Protocol::Auto;
    };

    uint16_t DefaultPort(Protocol protocol);
    // The port to use when connecting with VNC or a forced protocol: `port`, else the protocol's (Auto counts as VNC).
    uint16_t EffectivePort(const Address& address);
    const char* ProtocolName(Protocol protocol);

    // Accepts:
    //   host            -> Auto, usual ports
    //   host:N          -> VNC display N (port 5900 + N) when N < 100, otherwise Auto on port N
    //   host::port      -> Auto on that port
    //   [v6addr]:N      -> as above for IPv6 literals; a bare IPv6 literal (two or more colons) is a host
    //   marconi:// vnc:// rdp:// ssh://host[:port] -> that protocol; the number is a port
    //   user@...        -> any of the above, logging in as `user`
    // Returns false (and sets `error`) if the text can't be understood.
    bool ParseAddress(const std::string& text, Address& outAddress, std::string& error);

    // The inverse, for display and saving.
    std::string FormatAddress(const Address& address);
} // namespace Marconi
