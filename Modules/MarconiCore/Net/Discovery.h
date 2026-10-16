// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finds machines on the local network that advertise Screen Sharing (VNC), RDP or SSH over mDNS

#pragma once

#include <MarconiCore/Address.h>
#include <MarconiCore/Net/DnsMessage.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        struct DiscoveredService
        {
            std::string instance; // e.g. "Ford"
            std::string hostName; // e.g. "Mac.local"
            std::string ipv4;     // e.g. "192.168.1.20"; empty if no A record arrived
            uint16_t port = 0;
            Protocol protocol = Protocol::Auto;
        };

        // Turns PTR, SRV and A records into one service per advertised instance, in first-seen order.
        std::vector<DiscoveredService> AssembleServices(const std::vector<Dns::Record>& records);

        // Sends one "legacy unicast" mDNS query (from an ephemeral port, so responders answer us directly rather
        // than fighting the system's own mDNS service for port 5353) and collects answers for `listenMs`.
        // Returns nothing if the network doesn't allow it.
        std::vector<DiscoveredService> Discover(int listenMs, const std::string& destination = "224.0.0.251",
                                                uint16_t destinationPort = 5353,
                                                const std::atomic<bool>* cancel = nullptr);
    } // namespace Net
} // namespace Marconi
