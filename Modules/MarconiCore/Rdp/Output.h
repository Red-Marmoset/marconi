// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP server output: the bitmap update PDU (MS-RDPBCGR 2.2.9.1.1.3.1.2) that carries rectangles of the desktop, in
// both the slow path (a Share Data Update PDU) and the fast path (a lean header with no MCS/share wrapping). Each
// rectangle is handed to Rdp/Bitmap to decode. RDP milestone 4.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // One rectangle of the desktop to draw: where it goes, its size, and whether the pixels are run-length
        // compressed. `bitsPerPixel` is the rectangle's own depth; `data` is the bitmap stream.
        struct BitmapRect
        {
            uint16_t x = 0;
            uint16_t y = 0;
            uint16_t width = 0;
            uint16_t height = 0;
            uint16_t bitsPerPixel = 16;
            bool compressed = false;
            Bytes data;
        };

        // Parses a Bitmap Update's body (the part after TS_UPDATE_BITMAP's updateType field): a rectangle count
        // then that many TS_BITMAP_DATA. False if malformed.
        bool ParseBitmapUpdate(const Bytes& body, std::vector<BitmapRect>& out, std::string& error);

        // The kinds of fast-path update this recognises (MS-RDPBCGR 2.2.9.1.2.1).
        enum class FastPathUpdateType
        {
            Bitmap,  // rectangles to draw: `rects` filled in
            Pointer, // a pointer change: left to the caller (not parsed here yet)
            Other,   // surface commands, palette, synchronize, etc.: ignored for now
        };

        struct FastPathUpdate
        {
            FastPathUpdateType type = FastPathUpdateType::Other;
            std::vector<BitmapRect> rects;
        };

        // Parses the updates inside a fast-path output PDU's payload (everything after its header). A payload may
        // carry several updates; each is returned. False if malformed.
        bool ParseFastPathUpdates(const Bytes& payload, std::vector<FastPathUpdate>& out, std::string& error);
    } // namespace Rdp
} // namespace Marconi
