// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NSCodec (MS-RDPNSC): luma, orange and green chroma and alpha planes, each run-length coded, with colour loss and
// optional half-size chroma. ClearCodec carries it as one of its subcodecs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        // Decodes a `width` by `height` NSCodec bitmap into `out` (0x00RRGGBB, top row first). False with `error`
        // if malformed.
        bool DecodeNsc(const uint8_t* data, size_t size, int width, int height, std::vector<uint32_t>& out,
                       std::string& error);
    } // namespace Rdp
} // namespace Marconi
