// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Writing screenshots as PNG files, with wx's bundled zlib

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // Encodes 0x00RRGGBB pixels (`stride` pixels from one row to the next) as an 8-bit RGB PNG. Empty if the
        // size is zero or compressing fails.
        std::vector<uint8_t> EncodePng(const uint32_t* pixels, int width, int height, size_t stride);
    } // namespace Mcp
} // namespace Marconi
