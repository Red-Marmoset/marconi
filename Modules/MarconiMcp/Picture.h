// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Screenshot pictures: copying part of the desktop, drawing the cursor in, and shrinking to a size AI models take

#pragma once

#include <MarconiCore/RemoteSession.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // 0x00RRGGBB pixels, rows packed (stride = width).
        struct Picture
        {
            int width = 0;
            int height = 0;
            std::vector<uint32_t> pixels;
        };

        struct PictureSize
        {
            int width = 0;
            int height = 0;
        };

        // The largest size with the same shape as width x height that fits within `maxSide` on each side and
        // `maxPixels` in all. Never larger than the original, never smaller than 1x1.
        PictureSize FitWithin(int width, int height, int maxSide, long long maxPixels);

        // Copies the area (x, y, width, height) of `pixels` (`stride` pixels per row). The area must lie inside.
        Picture CopyArea(const uint32_t* pixels, size_t stride, int x, int y, int width, int height);

        // Draws the cursor with its hotspot at (x, y) in the picture's own coordinates; the parts outside are
        // clipped. Pixels whose alpha is 0 are left alone.
        void DrawCursor(Picture& picture, const CursorShape& cursor, int x, int y);

        // Shrinks `source` to outWidth x outHeight, each output pixel the average of the source pixels it covers
        // (sharper text than picking one pixel). Sizes larger than the source are clamped to it.
        Picture ScaleDown(const Picture& source, int outWidth, int outHeight);
    } // namespace Mcp
} // namespace Marconi
