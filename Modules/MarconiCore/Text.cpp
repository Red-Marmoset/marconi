// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Text encoding helpers: RFB uses Latin-1 for passwords and clipboard text, the UI uses UTF-8

#include <MarconiCore/Text.h>

#include <cstdint>

namespace Marconi
{
    std::string Utf8ToLatin1(const std::string& utf8)
    {
        std::string latin1;
        latin1.reserve(utf8.size());
        for (size_t i = 0; i < utf8.size();)
        {
            uint8_t lead = static_cast<uint8_t>(utf8[i]);
            uint32_t codepoint = 0;
            size_t length = 0;
            if (lead < 0x80)
            {
                codepoint = lead;
                length = 1;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                codepoint = lead & 0x1F;
                length = 2;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                codepoint = lead & 0x0F;
                length = 3;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                codepoint = lead & 0x07;
                length = 4;
            }

            bool valid = length > 0 && i + length <= utf8.size();
            for (size_t j = 1; valid && j < length; ++j)
            {
                uint8_t continuation = static_cast<uint8_t>(utf8[i + j]);
                valid = (continuation & 0xC0) == 0x80;
                codepoint = (codepoint << 6) | (continuation & 0x3F);
            }

            if (!valid)
            {
                latin1.push_back(static_cast<char>(lead)); // not UTF-8: assume it was Latin-1 already
                ++i;
                continue;
            }
            latin1.push_back(codepoint <= 0xFF ? static_cast<char>(codepoint) : '?');
            i += length;
        }
        return latin1;
    }

    std::string Latin1ToUtf8(const std::string& latin1)
    {
        std::string utf8;
        utf8.reserve(latin1.size());
        for (char c : latin1)
        {
            uint8_t byte = static_cast<uint8_t>(c);
            if (byte < 0x80)
            {
                utf8.push_back(c);
            }
            else
            {
                utf8.push_back(static_cast<char>(0xC0 | (byte >> 6)));
                utf8.push_back(static_cast<char>(0x80 | (byte & 0x3F)));
            }
        }
        return utf8;
    }

    std::string ToNetworkLineEndings(const std::string& text)
    {
        std::string result;
        result.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            {
                continue; // drop the CR of CRLF
            }
            result.push_back(text[i]);
        }
        return result;
    }

    std::string BytesToHex(const std::vector<uint8_t>& bytes)
    {
        static const char kDigits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(bytes.size() * 2);
        for (uint8_t byte : bytes)
        {
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 15]);
        }
        return hex;
    }

    std::vector<uint8_t> HexToBytes(const std::string& hex)
    {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        };
        std::vector<uint8_t> bytes;
        int pending = -1;
        for (char c : hex)
        {
            if (c == ':' || c == ' ' || c == '-')
            {
                continue;
            }
            int value = digit(c);
            if (value < 0)
            {
                return {};
            }
            if (pending < 0)
            {
                pending = value;
            }
            else
            {
                bytes.push_back(static_cast<uint8_t>((pending << 4) | value));
                pending = -1;
            }
        }
        if (pending >= 0)
        {
            return {}; // odd number of digits
        }
        return bytes;
    }
} // namespace Marconi
