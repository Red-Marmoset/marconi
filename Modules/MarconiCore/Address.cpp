// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Parsing of user-typed addresses ("host", "user@host", "host:display", "host::port", "rdp://host", "[ipv6]:display")

#include <MarconiCore/Address.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Marconi
{
    namespace
    {
        struct Scheme
        {
            const char* prefix;
            Protocol protocol;
        };
        constexpr Scheme kSchemes[] = {{"marconi://", Protocol::Marconi},
                                       {"vnc://", Protocol::Vnc},
                                       {"rdp://", Protocol::Rdp},
                                       {"ssh://", Protocol::Ssh}};

        bool StartsWithNoCase(const std::string& text, const char* prefix)
        {
            size_t length = std::strlen(prefix);
            return text.size() >= length &&
                   std::equal(text.begin(), text.begin() + static_cast<ptrdiff_t>(length), prefix,
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
        }

        std::string Trim(const std::string& text)
        {
            size_t begin = 0;
            size_t end = text.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
            {
                ++begin;
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
            {
                --end;
            }
            return text.substr(begin, end - begin);
        }

        bool ParseNumber(const std::string& text, long& out)
        {
            if (text.empty() || text.size() > 5 ||
                !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            {
                return false;
            }
            out = std::stol(text);
            return true;
        }

        // `suffix` is what followed the host: "", ":N" or "::N". `urlForm` treats ":N" as a port; otherwise a small
        // ":N" is a VNC display number.
        bool ApplySuffix(const std::string& suffix, bool urlForm, uint16_t& port, Protocol& protocol,
                         std::string& error)
        {
            if (suffix.empty())
            {
                port = 0;
                return true;
            }
            bool explicitPort = suffix.compare(0, 2, "::") == 0;
            std::string digits = suffix.substr(explicitPort ? 2 : 1);
            long value = 0;
            if (!ParseNumber(digits, value))
            {
                error = "'" + digits + "' isn't a valid display or port number";
                return false;
            }
            if (!explicitPort && !urlForm && value < 100)
            {
                value += 5900;
                protocol = Protocol::Vnc;
            }
            if (value < 1 || value > 65535)
            {
                error = "Port " + std::to_string(value) + " is out of range";
                return false;
            }
            port = static_cast<uint16_t>(value);
            return true;
        }
    } // namespace

    bool ParseAddress(const std::string& input, Address& outAddress, std::string& error)
    {
        std::string text = Trim(input);
        bool urlForm = false;
        Protocol protocol = Protocol::Auto;
        for (const Scheme& scheme : kSchemes)
        {
            if (StartsWithNoCase(text, scheme.prefix))
            {
                text = text.substr(std::strlen(scheme.prefix));
                urlForm = true;
                protocol = scheme.protocol;
                while (!text.empty() && text.back() == '/')
                {
                    text.pop_back();
                }
                break;
            }
        }
        if (text.empty())
        {
            error = "Enter a computer name or address";
            return false;
        }

        Address address;
        size_t at = text.rfind('@');
        if (at != std::string::npos)
        {
            address.username = text.substr(0, at);
            text = text.substr(at + 1);
            if (address.username.empty())
            {
                error = "Enter a user name before the '@'";
                return false;
            }
            if (text.empty())
            {
                error = "Enter a computer name or address after the '@'";
                return false;
            }
        }

        std::string suffix;
        if (text[0] == '[')
        {
            size_t close = text.find(']');
            if (close == std::string::npos || close == 1)
            {
                error = "Unterminated IPv6 address";
                return false;
            }
            address.host = text.substr(1, close - 1);
            suffix = text.substr(close + 1);
            if (!suffix.empty() && suffix[0] != ':')
            {
                error = "Unexpected text after IPv6 address";
                return false;
            }
        }
        else
        {
            size_t first = text.find(':');
            size_t doubleColon = text.find("::");
            size_t colonCount = static_cast<size_t>(std::count(text.begin(), text.end(), ':'));
            if (first == std::string::npos)
            {
                address.host = text;
            }
            else if (colonCount == 1 || (colonCount == 2 && doubleColon == first))
            {
                address.host = text.substr(0, first);
                suffix = text.substr(first);
            }
            else
            {
                address.host = text; // bare IPv6 literal
            }
        }

        if (address.host.empty())
        {
            error = "Enter a computer name or address";
            return false;
        }
        if (address.host.find_first_of(" \t/") != std::string::npos)
        {
            error = "'" + address.host + "' isn't a valid computer name";
            return false;
        }
        address.protocol = protocol;
        if (!ApplySuffix(suffix, urlForm, address.port, address.protocol, error))
        {
            return false;
        }
        outAddress = address;
        return true;
    }

    uint16_t DefaultPort(Protocol protocol)
    {
        switch (protocol)
        {
            case Protocol::Marconi: return 1895; // Wire::kDefaultPort
            case Protocol::Vnc: return 5900;
            case Protocol::Rdp: return 3389;
            case Protocol::Ssh: return 22;
            case Protocol::Auto: break;
        }
        return 0;
    }

    uint16_t EffectivePort(const Address& address)
    {
        if (address.port != 0)
        {
            return address.port;
        }
        return DefaultPort(address.protocol == Protocol::Auto ? Protocol::Vnc : address.protocol);
    }

    const char* ProtocolName(Protocol protocol)
    {
        switch (protocol)
        {
            case Protocol::Marconi: return "Marconi";
            case Protocol::Vnc: return "VNC";
            case Protocol::Rdp: return "Remote Desktop (RDP)";
            case Protocol::Ssh: return "SSH";
            case Protocol::Auto: break;
        }
        return "automatic";
    }

    std::string FormatAddress(const Address& address)
    {
        bool ipv6 = address.host.find(':') != std::string::npos;
        std::string host = (address.username.empty() ? "" : address.username + "@") +
                           (ipv6 ? "[" + address.host + "]" : address.host);
        if (address.protocol == Protocol::Vnc && address.port > 5900 && address.port < 6000)
        {
            return host + ":" + std::to_string(address.port - 5900); // display number
        }
        bool showPort = address.port != 0 && address.port != DefaultPort(address.protocol);
        if (address.protocol == Protocol::Auto)
        {
            return showPort ? host + "::" + std::to_string(address.port) : host;
        }
        const char* scheme = address.protocol == Protocol::Marconi ? "marconi://"
                             : address.protocol == Protocol::Vnc   ? "vnc://"
                             : address.protocol == Protocol::Rdp   ? "rdp://"
                                                                   : "ssh://";
        return scheme + host + (showPort ? ":" + std::to_string(address.port) : "");
    }
} // namespace Marconi
