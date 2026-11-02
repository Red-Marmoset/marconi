// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The RemoteFX Progressive codec (MS-RDPEGFX 2.2.4.2), the picture format GNOME Remote Desktop, KDE's KRdp and
// Windows send over the graphics pipeline when H.264 is off: 64x64 tiles of RLGR-coded wavelet coefficients in
// YCbCr, sent whole ("simple" tiles), or as a first pass followed by refinement passes ("upgrade" tiles).

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        // The quantisation of each of a tile's ten wavelet bands.
        struct ProgressiveQuant
        {
            int ll3 = 0, lh3 = 0, hl3 = 0, hh3 = 0;
            int lh2 = 0, hl2 = 0, hh2 = 0;
            int lh1 = 0, hl1 = 0, hh1 = 0;
        };

        // What a surface remembers between messages: the codec's context flags and, for each tile it has been
        // sent, the wavelet coefficients so far (refinement passes and "difference" tiles build on them).
        class ProgressiveSurface
        {
        public:
            struct Tile
            {
                std::array<std::array<int16_t, 4096>, 3> current{}; // Y, Cb, Cr coefficients, dequantised
                std::array<std::array<int16_t, 4096>, 3> sign{};    // which coefficients are known to be nonzero
                std::array<ProgressiveQuant, 3> bitPos{};           // how many low bits are still missing
                int pass = 0;
            };

            bool subbandDiffing = false; // from the context block, which comes once per surface
            std::map<uint32_t, std::unique_ptr<Tile>> tiles; // by yIdx * 65536 + xIdx
        };

        // The part of the surface a message changed (surface coordinates, right/bottom exclusive).
        struct ProgressiveRect
        {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
        };

        // Decodes one Progressive message into `pixels` (0x00RRGGBB, `width` by `height`, `stride` pixels per row),
        // drawing each tile only inside the rectangles its region names. `updated` gets the areas drawn. False with
        // `error` if the message is malformed.
        bool DecodeProgressive(const uint8_t* data, size_t size, ProgressiveSurface& surface, uint32_t* pixels,
                               int width, int height, int stride, std::vector<ProgressiveRect>& updated,
                               std::string& error);
    } // namespace Rdp
} // namespace Marconi
