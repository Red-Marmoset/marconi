// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turns probe results into a decision: which protocol to connect with, or what to tell the user

#pragma once

#include <MarconiCore/Address.h>
#include <MarconiCore/Net/Probe.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        struct Choice
        {
            enum class Kind
            {
                Connect,      // use `protocol` on `port`
                Unsupported,  // found only protocols Marconi can't speak yet
                NothingFound, // nothing recognisable answered
            };
            Kind kind = Kind::NothingFound;
            Protocol protocol = Protocol::Auto; // Connect: what to use
            uint16_t port = 0;                  // Connect: where
            std::vector<ProbeResult> found;     // every reachable result, best first
            std::vector<ProbeResult> fallbacks; // Connect: the other supported results, best first, to try in turn
                                                // if `protocol` can't connect
            std::string error;                  // NothingFound because the name couldn't be resolved: that error
        };

        // Whether Marconi can connect with `protocol` today.
        bool IsSupported(Protocol protocol);

        // Picks the best supported protocol among `results` (preference: Marconi, RDP, VNC, SSH), honouring a forced
        // protocol.
        Choice ChooseProtocol(const Address& address, const std::vector<ProbeResult>& results);

        // The message to show the user, or "" for Connect.
        std::string DescribeChoice(const Choice& choice, const Address& address);
    } // namespace Net
} // namespace Marconi
