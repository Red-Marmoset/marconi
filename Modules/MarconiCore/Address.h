// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Parsing of user-typed VNC addresses ("host", "user@host", "host:display", "host::port", "[ipv6]:display")

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    struct Address
    {
        std::string host;
        uint16_t port = 5900;
        std::string username; // from "user@host": log in with that account (Apple Remote Desktop)
    };

    // Accepts the conventions used by other VNC viewers:
    //   host            -> port 5900
    //   host:N          -> display N, port 5900 + N when N < 100, otherwise N is the port itself
    //   host::port      -> explicit port
    //   [v6addr]:N      -> as above for IPv6 literals; a bare IPv6 literal (two or more colons) is a host
    //   vnc://host:port -> URL form, port is a real port
    //   user@...        -> any of the above, logging in as `user`
    // Returns false (and sets `error`) if the text can't be understood.
    bool ParseAddress(const std::string& text, Address& outAddress, std::string& error);

    // The inverse, for display and saving: "host" when the port is 5900, else "host::port"; with "user@" if set.
    std::string FormatAddress(const Address& address);
} // namespace Marconi
