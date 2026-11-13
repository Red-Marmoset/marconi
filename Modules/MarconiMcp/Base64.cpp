// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Base64, for images in MCP messages

#include <MarconiMcp/Base64.h>

namespace Marconi
{
    namespace Mcp
    {
        std::string Base64Encode(const std::vector<uint8_t>& data)
        {
            static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve((data.size() + 2) / 3 * 4);
            size_t i = 0;
            for (; i + 3 <= data.size(); i += 3)
            {
                const uint32_t group = (static_cast<uint32_t>(data[i]) << 16) |
                                       (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
                out += kAlphabet[(group >> 18) & 63];
                out += kAlphabet[(group >> 12) & 63];
                out += kAlphabet[(group >> 6) & 63];
                out += kAlphabet[group & 63];
            }
            const size_t left = data.size() - i;
            if (left > 0)
            {
                uint32_t group = static_cast<uint32_t>(data[i]) << 16;
                if (left == 2)
                {
                    group |= static_cast<uint32_t>(data[i + 1]) << 8;
                }
                out += kAlphabet[(group >> 18) & 63];
                out += kAlphabet[(group >> 12) & 63];
                out += left == 2 ? kAlphabet[(group >> 6) & 63] : '=';
                out += '=';
            }
            return out;
        }
    } // namespace Mcp
} // namespace Marconi
