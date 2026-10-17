// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Wake-on-LAN: waking a sleeping computer on the local network with its hardware (MAC) address

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        using MacAddress = std::array<uint8_t, 6>;

        // Accepts "AA:BB:CC:DD:EE:FF", "AA-BB-CC-DD-EE-FF", "AABB.CCDD.EEFF" or "AABBCCDDEEFF", any case.
        bool ParseMacAddress(const std::string& text, MacAddress& out);
        std::string FormatMacAddress(const MacAddress& mac);

        // The "magic packet": six 0xFF bytes followed by the address sixteen times.
        std::vector<uint8_t> BuildMagicPacket(const MacAddress& mac);

        // Broadcasts the magic packet on every network this computer is on (UDP port 9). False with `error` if
        // the address doesn't parse or the packet couldn't be sent. Whether the computer wakes is up to it.
        bool SendWakeOnLan(const std::string& macText, std::string& error);
    } // namespace Net
} // namespace Marconi
