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
        // compressed. `bitsPerPixel` is the rectangle's own depth; `data` is the bitmap stream. `width`/`height`
        // are the bitmap's own size, which may be a little larger than the area it fills (`destWidth`/
        // `destHeight`, from the inclusive destination rectangle): draw the top-left part.
        struct BitmapRect
        {
            uint16_t x = 0;
            uint16_t y = 0;
            uint16_t width = 0;
            uint16_t height = 0;
            uint16_t destWidth = 0;
            uint16_t destHeight = 0;
            uint16_t bitsPerPixel = 16;
            bool compressed = false;
            Bytes data;
        };

        // Parses a Bitmap Update's body (the part after TS_UPDATE_BITMAP's updateType field): a rectangle count
        // then that many TS_BITMAP_DATA. False if malformed.
        bool ParseBitmapUpdate(const Bytes& body, std::vector<BitmapRect>& out, std::string& error);

        // Parses a Palette Update's body (the part after TS_UPDATE_PALETTE's updateType field) into 256 entries of
        // 0x00RRGGBB, for 8-bit bitmaps. False if malformed.
        bool ParsePaletteUpdate(const Bytes& body, std::vector<uint32_t>& palette, std::string& error);

        // What a slow-path Update PDU (ShareDataType::Update) carries (its updateType, MS-RDPBCGR 2.2.9.1.1.3).
        enum class UpdateType
        {
            Bitmap,      // `rects` filled in
            Palette,     // `palette` filled in
            Orders,      // drawing orders: not asked for, ignored if they come
            Synchronize, // nothing to do
            Other,
        };

        struct Update
        {
            UpdateType type = UpdateType::Other;
            std::vector<BitmapRect> rects;
            std::vector<uint32_t> palette;
        };

        // Parses a slow-path Update PDU's body (updateType, then the update's data). False if malformed.
        bool ParseUpdatePdu(const Bytes& body, Update& out, std::string& error);

        // The kinds of fast-path update this recognises (MS-RDPBCGR 2.2.9.1.2.1).
        enum class FastPathUpdateType
        {
            Bitmap,  // rectangles to draw: `rects` filled in
            Palette, // `palette` filled in
            Pointer, // a pointer change: `code` and `data` go to Rdp::ParseFastPathPointer
            Other,   // orders, surface commands, synchronize, etc.: ignored for now
        };

        struct FastPathUpdate
        {
            FastPathUpdateType type = FastPathUpdateType::Other;
            uint8_t code = 0; // the updateCode
            std::vector<BitmapRect> rects;
            std::vector<uint32_t> palette;
            Bytes data; // the update's payload, for the kinds not parsed here
        };

        // Parses the updates inside a fast-path output PDU's payload (everything after its header). A payload may
        // carry several updates; each is returned. False if malformed.
        bool ParseFastPathUpdates(const Bytes& payload, std::vector<FastPathUpdate>& out, std::string& error);
    } // namespace Rdp
} // namespace Marconi
