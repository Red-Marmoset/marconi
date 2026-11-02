// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The planar bitmap codec (MS-RDPEGDI 2.2.2.5.1, 3.1.9): separate colour planes, raw or run-length coded with
// deltas between rows, in RGB or YCoCg (optionally with half-resolution chroma). Used by the graphics pipeline
// (codec 0x000A) and for 32-bit pictures in the classic bitmap updates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        // Decodes a `width` by `height` planar bitmap into `out` (0x00RRGGBB, top row first). `bottomUp`: the
        // rows are stored bottom first, as in the classic bitmap updates (the graphics pipeline stores them top
        // first). The alpha plane, when there is one, is read past and dropped. False with `error` if malformed.
        bool DecodePlanar(const uint8_t* data, size_t size, int width, int height, bool bottomUp,
                          std::vector<uint32_t>& out, std::string& error);
    } // namespace Rdp
} // namespace Marconi
