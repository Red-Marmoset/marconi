// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finds which remote-desktop services a host offers by connecting to ports and reading their greetings

#pragma once

#include <MarconiCore/Address.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        struct ProbeTarget
        {
            uint16_t port = 0;
            Protocol expected = Protocol::Auto; // Auto: identify by greeting, trying RDP if the server stays silent
        };

        struct ProbeResult
        {
            uint16_t port = 0;
            bool reachable = false;             // something accepted the connection
            Protocol protocol = Protocol::Auto; // Auto: nothing we recognise
            std::string detail;                 // e.g. "RFB 003.889", "SSH-2.0-OpenSSH_9.6", or why it failed
        };

        // The ports to try for an address: its own port if it has one, else the usual VNC, RDP and SSH ports.
        std::vector<ProbeTarget> DefaultProbeTargets(const Address& address);

        // Probes every target in parallel and returns one result per target, in the same order. Never authenticates.
        std::vector<ProbeResult> Probe(const std::string& host, const std::vector<ProbeTarget>& targets, int timeoutMs,
                                       const std::atomic<bool>* cancel = nullptr);

        Protocol IdentifyGreeting(const std::string& greeting);
        bool IsRdpConnectionConfirm(const uint8_t* data, size_t size);
    } // namespace Net
} // namespace Marconi
