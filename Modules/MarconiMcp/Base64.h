// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Base64, for images in MCP messages

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // Standard alphabet, with '=' padding.
        std::string Base64Encode(const std::vector<uint8_t>& data);
    } // namespace Mcp
} // namespace Marconi
