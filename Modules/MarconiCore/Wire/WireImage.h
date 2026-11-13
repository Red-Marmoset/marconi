// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clipboard images: the wire carries PNG, the viewer's clipboard works in Windows bitmaps (DIB); these convert

#pragma once

#include <MarconiCore/Wire/WireStream.h>

namespace Marconi
{
    namespace Wire
    {
        // The biggest picture either side will convert, so a hostile image can't make a huge allocation.
        constexpr int kMaxImageDimension = 16384;
        constexpr uint64_t kMaxImagePixels = 64ull * 1024 * 1024;

        // Turns a device-independent bitmap (BITMAPINFOHEADER and pixels, no file header) into a PNG. Reads 24 and
        // 32 bits per pixel, plain or with bit masks. False for any other form (palettes, 16 bits, compression).
        bool DibToPng(const Bytes& dib, Bytes& png);

        // Turns a PNG (any colour type) into a 24-bit bottom-up DIB, with transparency flattened onto white.
        // False if the data isn't a PNG this build can read or the picture is too large.
        bool PngToDib(const Bytes& png, Bytes& dib);

        // Writes a PNG from top-down 8-bit RGB rows (`rgb` is width*height*3 bytes). Used by DibToPng and tests.
        bool EncodePng(const uint8_t* rgb, int width, int height, Bytes& png);
    } // namespace Wire
} // namespace Marconi
