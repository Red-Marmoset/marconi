// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Who may connect: a list of addresses and networks (IPv4 and IPv6, single addresses and CIDR ranges)

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // One entry of the list: an address with a prefix length. Everything is held as 16 bytes (an IPv4 address as
        // its IPv4-mapped IPv6 form, ::ffff:a.b.c.d, with the prefix length moved up by 96), so an IPv4 viewer that
        // reaches a dual-stack socket as ::ffff:a.b.c.d still matches an entry written as a.b.c.d.
        // Note that 127.0.0.1 and ::1 are different addresses: an entry for one does not allow the other.
        struct AddressRule
        {
            std::array<uint8_t, 16> bytes{};
            int prefixBits = 128; // how many leading bits (of the 128) must match
        };

        // Reads one entry such as "192.168.1.20", "192.168.1.0/24", "::1" or "fd00::/8". Spaces around it are
        // ignored. False (with `error`, in plain words) when it isn't an address or a network.
        bool ParseAddressRule(const std::string& text, AddressRule& rule, std::string& error);

        // Reads a numeric address (no network part) into its 16-byte form. False if it isn't one.
        bool ParseAddress(const std::string& text, std::array<uint8_t, 16>& bytes);

        bool RuleMatches(const AddressRule& rule, const std::array<uint8_t, 16>& address);

        // The allow list. An empty list lets everyone in (the server's behaviour before there was a list). A list
        // with entries lets in only the addresses that match one; an address that can't be read never matches.
        class AllowList
        {
        public:
            AllowList() = default;

            // Replaces the list. False (with `error` naming the first bad entry) and the list left unchanged if any
            // entry can't be read. Blank entries are skipped.
            bool Set(const std::vector<std::string>& entries, std::string& error);

            bool IsEmpty() const { return m_rules.empty(); }
            bool Allows(const std::string& address) const;
            const std::vector<std::string>& Entries() const { return m_entries; }

        private:
            std::vector<AddressRule> m_rules;
            std::vector<std::string> m_entries; // as given, trimmed
        };

        // Splits text typed by a person (one entry per line, or separated by commas, spaces or semicolons) into
        // entries, skipping blanks.
        std::vector<std::string> SplitAllowEntries(const std::string& text);
    } // namespace Server
} // namespace Marconi
