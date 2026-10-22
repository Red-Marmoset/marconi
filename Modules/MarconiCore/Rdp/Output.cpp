// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP server output: parsing bitmap update PDUs (slow path and fast path) into rectangles.

#include <MarconiCore/Rdp/Output.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            uint16_t GetU16(const Bytes& data, size_t offset)
            {
                return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            }

            // TS_BITMAP_DATA flags.
            constexpr uint16_t kBitmapCompression = 0x0001;
            constexpr uint16_t kNoBitmapCompressionHeader = 0x0400;

            // Reads the count-prefixed list of TS_BITMAP_DATA starting at `p` in `data`.
            bool ReadRectangles(const Bytes& data, size_t p, uint16_t count, std::vector<BitmapRect>& out,
                                std::string& error)
            {
                for (uint16_t i = 0; i < count; ++i)
                {
                    if (p + 18 > data.size())
                    {
                        error = "a bitmap update rectangle header ran past the end";
                        return false;
                    }
                    BitmapRect rect;
                    uint16_t left = GetU16(data, p);
                    uint16_t top = GetU16(data, p + 2);
                    uint16_t right = GetU16(data, p + 4);
                    uint16_t bottom = GetU16(data, p + 6);
                    rect.width = GetU16(data, p + 8);
                    rect.height = GetU16(data, p + 10);
                    rect.bitsPerPixel = GetU16(data, p + 12);
                    uint16_t flags = GetU16(data, p + 14);
                    uint16_t bitmapLength = GetU16(data, p + 16);
                    rect.x = left;
                    rect.y = top;
                    rect.destWidth = rect.width;
                    rect.destHeight = rect.height;
                    if (right >= left && bottom >= top)
                    {
                        // The right/bottom are inclusive; width/height usually match but trust the explicit size.
                        rect.destWidth = static_cast<uint16_t>(right - left + 1);
                        rect.destHeight = static_cast<uint16_t>(bottom - top + 1);
                        if (rect.width == 0)
                        {
                            rect.width = rect.destWidth;
                        }
                        if (rect.height == 0)
                        {
                            rect.height = rect.destHeight;
                        }
                    }
                    rect.compressed = (flags & kBitmapCompression) != 0;
                    p += 18;
                    size_t dataLength = bitmapLength;
                    if (rect.compressed && (flags & kNoBitmapCompressionHeader) == 0)
                    {
                        // An 8-byte compressed-data header precedes the stream; skip it (its cbCompMainBodySize is
                        // the real length, but bitmapLength already covers header + body).
                        if (p + 8 > data.size() || bitmapLength < 8)
                        {
                            error = "a compressed bitmap was shorter than its header";
                            return false;
                        }
                        uint16_t mainBody = GetU16(data, p + 2);
                        p += 8;
                        dataLength = mainBody;
                    }
                    if (p + dataLength > data.size())
                    {
                        error = "a bitmap update rectangle ran past the end";
                        return false;
                    }
                    rect.data.assign(data.begin() + p, data.begin() + p + dataLength);
                    p += dataLength;
                    out.push_back(std::move(rect));
                }
                return true;
            }
        } // namespace

        bool ParseBitmapUpdate(const Bytes& body, std::vector<BitmapRect>& out, std::string& error)
        {
            if (body.size() < 2)
            {
                error = "a bitmap update had no rectangle count";
                return false;
            }
            uint16_t count = GetU16(body, 0);
            return ReadRectangles(body, 2, count, out, error);
        }

        bool ParsePaletteUpdate(const Bytes& body, std::vector<uint32_t>& palette, std::string& error)
        {
            // TS_UPDATE_PALETTE after updateType: pad2Octets (2), numberColors (4), then RGB triples.
            if (body.size() < 6)
            {
                error = "a palette update had no colour count";
                return false;
            }
            uint32_t count = static_cast<uint32_t>(body[2]) | (static_cast<uint32_t>(body[3]) << 8) |
                             (static_cast<uint32_t>(body[4]) << 16) | (static_cast<uint32_t>(body[5]) << 24);
            if (count > 256 || body.size() < 6 + static_cast<size_t>(count) * 3)
            {
                error = "a palette update's colours ran past the end";
                return false;
            }
            palette.assign(256, 0);
            for (uint32_t i = 0; i < count; ++i)
            {
                size_t p = 6 + static_cast<size_t>(i) * 3;
                palette[i] = (static_cast<uint32_t>(body[p]) << 16) | (static_cast<uint32_t>(body[p + 1]) << 8) |
                             body[p + 2];
            }
            return true;
        }

        bool ParseUpdatePdu(const Bytes& body, Update& out, std::string& error)
        {
            if (body.size() < 2)
            {
                error = "an update PDU had no update type";
                return false;
            }
            Bytes rest(body.begin() + 2, body.end());
            switch (GetU16(body, 0))
            {
                case 0x0000:
                    out.type = UpdateType::Orders;
                    return true;
                case 0x0001:
                    out.type = UpdateType::Bitmap;
                    return ParseBitmapUpdate(rest, out.rects, error);
                case 0x0002:
                    out.type = UpdateType::Palette;
                    return ParsePaletteUpdate(rest, out.palette, error);
                case 0x0003:
                    out.type = UpdateType::Synchronize;
                    return true;
                default:
                    out.type = UpdateType::Other;
                    return true;
            }
        }

        bool ParseFastPathUpdates(const Bytes& payload, std::vector<FastPathUpdate>& out, std::string& error)
        {
            size_t p = 0;
            while (p < payload.size())
            {
                if (p + 3 > payload.size())
                {
                    error = "a fast-path update header ran past the end";
                    return false;
                }
                uint8_t header = payload[p];
                uint8_t code = header & 0x0F;       // updateCode
                uint8_t fragmentation = (header >> 4) & 0x03;
                uint16_t size = GetU16(payload, p + 1);
                p += 3;
                if (p + size > payload.size())
                {
                    error = "a fast-path update ran past the end";
                    return false;
                }
                (void)fragmentation; // single-fragment updates only for now
                FastPathUpdate update;
                update.code = code;
                update.data.assign(payload.begin() + p, payload.begin() + p + size);
                if (code == 0x01 || code == 0x02) // FASTPATH_UPDATETYPE_BITMAP / _PALETTE
                {
                    // The payload is a TS_UPDATE_BITMAP / TS_UPDATE_PALETTE: a 2-byte updateType then the data.
                    if (update.data.size() < 2)
                    {
                        error = "a fast-path update was too short";
                        return false;
                    }
                    Bytes afterType(update.data.begin() + 2, update.data.end());
                    if (code == 0x01)
                    {
                        update.type = FastPathUpdateType::Bitmap;
                        if (!ParseBitmapUpdate(afterType, update.rects, error))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        update.type = FastPathUpdateType::Palette;
                        if (!ParsePaletteUpdate(afterType, update.palette, error))
                        {
                            return false;
                        }
                    }
                }
                else if (code >= 0x05 && code <= 0x0C && code != 0x07) // pointer updates: PTR_NULL to LARGE_POINTER
                {
                    update.type = FastPathUpdateType::Pointer;
                }
                else
                {
                    update.type = FastPathUpdateType::Other;
                }
                out.push_back(std::move(update));
                p += size;
            }
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
