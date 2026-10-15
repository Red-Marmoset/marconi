// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Parsing of user-typed VNC addresses ("host", "host:display", "host::port", "[ipv6]:display")

#include <RmVncCore/Address.h>

#include <algorithm>
#include <cctype>

namespace RmVnc
{
    namespace
    {
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

        // `suffix` is what followed the host: "", ":N" or "::N". `urlForm` treats ":N" as a port.
        bool ApplySuffix(const std::string& suffix, bool urlForm, uint16_t& port, std::string& error)
        {
            if (suffix.empty())
            {
                port = 5900;
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
        if (text.size() >= 6 && std::equal(text.begin(), text.begin() + 6, "vnc://",
                                           [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
        {
            text = text.substr(6);
            urlForm = true;
            while (!text.empty() && text.back() == '/')
            {
                text.pop_back();
            }
        }
        if (text.empty())
        {
            error = "Enter a computer name or address";
            return false;
        }

        Address address;
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
        if (!ApplySuffix(suffix, urlForm, address.port, error))
        {
            return false;
        }
        outAddress = address;
        return true;
    }

    std::string FormatAddress(const Address& address)
    {
        bool ipv6 = address.host.find(':') != std::string::npos;
        std::string host = ipv6 ? "[" + address.host + "]" : address.host;
        if (address.port == 5900)
        {
            return host;
        }
        return host + "::" + std::to_string(address.port);
    }
} // namespace RmVnc
