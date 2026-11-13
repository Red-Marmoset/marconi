// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The allow list parser and matcher

#include "AllowList.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <cstring>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

            std::string Trim(const std::string& text)
            {
                size_t begin = 0;
                size_t end = text.size();
                while (begin < end && IsSpace(text[begin]))
                {
                    ++begin;
                }
                while (end > begin && IsSpace(text[end - 1]))
                {
                    --end;
                }
                return text.substr(begin, end - begin);
            }

            // True when the text is one to three digits and nothing else, as a prefix length is.
            bool ReadPrefix(const std::string& text, int& value)
            {
                if (text.empty() || text.size() > 3)
                {
                    return false;
                }
                value = 0;
                for (char c : text)
                {
                    if (c < '0' || c > '9')
                    {
                        return false;
                    }
                    value = value * 10 + (c - '0');
                }
                return true;
            }
        } // namespace

        bool ParseAddress(const std::string& text, std::array<uint8_t, 16>& bytes)
        {
            if (text.empty() || text.size() > 45)
            {
                return false;
            }
            if (text.find(':') == std::string::npos)
            {
                // Only the four-part dotted form: inet_pton accepts exactly that, unlike inet_aton.
                in_addr v4;
                if (inet_pton(AF_INET, text.c_str(), &v4) != 1)
                {
                    return false;
                }
                bytes.fill(0);
                bytes[10] = 0xFF;
                bytes[11] = 0xFF;
                std::memcpy(&bytes[12], &v4, 4);
                return true;
            }
            in6_addr v6;
            if (inet_pton(AF_INET6, text.c_str(), &v6) != 1)
            {
                return false;
            }
            std::memcpy(bytes.data(), &v6, 16);
            return true;
        }

        bool ParseAddressRule(const std::string& input, AddressRule& rule, std::string& error)
        {
            std::string text = Trim(input);
            if (text.empty())
            {
                error = "The entry is empty.";
                return false;
            }
            std::string addressPart = text;
            std::string prefixPart;
            bool hasPrefix = false;
            size_t slash = text.find('/');
            if (slash != std::string::npos)
            {
                addressPart = text.substr(0, slash);
                prefixPart = text.substr(slash + 1);
                hasPrefix = true;
            }
            std::array<uint8_t, 16> bytes{};
            if (!ParseAddress(addressPart, bytes))
            {
                error = "\"" + text + "\" is not an address or a network. Use something like 192.168.1.20, "
                        "192.168.1.0/24 or fd00::/8.";
                return false;
            }
            const bool isV4 = addressPart.find(':') == std::string::npos;
            int bits = isV4 ? 32 : 128;
            if (hasPrefix)
            {
                int prefix = 0;
                if (!ReadPrefix(prefixPart, prefix) || prefix > bits)
                {
                    error = "\"" + text + "\" has a network size after the slash that is not a number from 0 to " +
                            std::to_string(bits) + ".";
                    return false;
                }
                bits = prefix;
            }
            rule.prefixBits = isV4 ? bits + 96 : bits;
            // Clear the bits past the prefix so "192.168.1.5/24" means the whole 192.168.1.x network.
            for (int i = 0; i < 16; ++i)
            {
                int keep = rule.prefixBits - i * 8;
                if (keep >= 8)
                {
                    continue;
                }
                bytes[i] = keep <= 0 ? 0 : static_cast<uint8_t>(bytes[i] & (0xFF << (8 - keep)));
            }
            rule.bytes = bytes;
            return true;
        }

        bool RuleMatches(const AddressRule& rule, const std::array<uint8_t, 16>& address)
        {
            for (int i = 0; i < 16; ++i)
            {
                int keep = rule.prefixBits - i * 8;
                if (keep <= 0)
                {
                    break;
                }
                uint8_t mask = keep >= 8 ? 0xFF : static_cast<uint8_t>(0xFF << (8 - keep));
                if ((address[i] & mask) != (rule.bytes[i] & mask))
                {
                    return false;
                }
            }
            return true;
        }

        bool AllowList::Set(const std::vector<std::string>& entries, std::string& error)
        {
            std::vector<AddressRule> rules;
            std::vector<std::string> kept;
            for (const std::string& entry : entries)
            {
                std::string trimmed = Trim(entry);
                if (trimmed.empty())
                {
                    continue;
                }
                AddressRule rule;
                if (!ParseAddressRule(trimmed, rule, error))
                {
                    return false;
                }
                rules.push_back(rule);
                kept.push_back(trimmed);
            }
            m_rules = std::move(rules);
            m_entries = std::move(kept);
            return true;
        }

        bool AllowList::Allows(const std::string& address) const
        {
            if (m_rules.empty())
            {
                return true;
            }
            std::array<uint8_t, 16> bytes{};
            if (!ParseAddress(Trim(address), bytes))
            {
                return false; // can't tell who this is, so it isn't on the list
            }
            for (const AddressRule& rule : m_rules)
            {
                if (RuleMatches(rule, bytes))
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> SplitAllowEntries(const std::string& text)
        {
            std::vector<std::string> entries;
            std::string current;
            for (char c : text)
            {
                if (IsSpace(c) || c == ',' || c == ';')
                {
                    if (!current.empty())
                    {
                        entries.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!current.empty())
            {
                entries.push_back(current);
            }
            return entries;
        }
    } // namespace Server
} // namespace Marconi
