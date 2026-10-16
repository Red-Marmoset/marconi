// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encoding DNS queries and parsing DNS responses (RFC 1035, with name compression), for mDNS discovery

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        namespace Dns
        {
            constexpr uint16_t kTypeA = 1;
            constexpr uint16_t kTypePtr = 12;
            constexpr uint16_t kTypeTxt = 16;
            constexpr uint16_t kTypeSrv = 33;
            constexpr uint16_t kTypeAny = 255;

            struct Question
            {
                std::string name;
                uint16_t type = kTypePtr;
            };

            struct Record
            {
                std::string name;
                uint16_t type = 0;
                uint32_t ttl = 0;
                std::string target; // PTR: the pointed-to name; SRV: the host name
                uint16_t port = 0;  // SRV
                std::string ipv4;   // A, dotted
            };

            std::vector<uint8_t> EncodeQuery(uint16_t id, const std::vector<Question>& questions);
            // A response with every record in the answer section (for tests and fake responders).
            std::vector<uint8_t> EncodeResponse(uint16_t id, const std::vector<Record>& answers);
            // Collects the A, PTR, SRV and TXT records from every section. False if the message is malformed.
            bool ParseMessage(const uint8_t* data, size_t size, std::vector<Record>& outRecords);
        } // namespace Dns
    } // namespace Net
} // namespace Marconi
