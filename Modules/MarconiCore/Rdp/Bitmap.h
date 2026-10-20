// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP's classic bitmap updates: uncompressed rectangles and the interleaved run-length encoding (MS-RDPBCGR
// 2.2.9.1.1.3.1.2.4 and 3.1.9), decoded into top-down 0x00RRGGBB pixels for the framebuffer. RDP milestone 4 in
// Documentation/RDP.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        // Colour depths the classic bitmap path carries. 32-bit pictures use a different codec (planar), not this.
        // `palette` is needed for 8 bits per pixel only: 256 entries of 0x00RRGGBB.
        //
        // Both functions fill `out` with width * height pixels, top row first (RDP sends rows bottom first). False
        // with `error` for a bad size, a depth these don't handle, or data that is truncated or would run past the
        // rectangle; nothing is written past the rectangle either way.

        // A run-length compressed rectangle: the bitmap stream only, without the 8-byte compressed-data header some
        // updates put in front (the bitmap update parser strips it).
        bool DecodeInterleavedRle(const uint8_t* data, size_t length, int width, int height, int bitsPerPixel,
                                  const uint32_t* palette, std::vector<uint32_t>& out, std::string& error);

        // An uncompressed rectangle: rows bottom first, each `width` pixels, optionally padded to a 4-byte
        // multiple (both layouts are accepted, told apart by the length).
        bool DecodeUncompressedBitmap(const uint8_t* data, size_t length, int width, int height, int bitsPerPixel,
                                      const uint32_t* palette, std::vector<uint32_t>& out, std::string& error);
    } // namespace Rdp
} // namespace Marconi
