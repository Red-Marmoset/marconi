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
                    if (right >= left && bottom >= top)
                    {
                        // The right/bottom are inclusive; width/height usually match but trust the explicit size.
                        if (rect.width == 0)
                        {
                            rect.width = static_cast<uint16_t>(right - left + 1);
                        }
                        if (rect.height == 0)
                        {
                            rect.height = static_cast<uint16_t>(bottom - top + 1);
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
                if (code == 0x01) // FASTPATH_UPDATETYPE_BITMAP
                {
                    update.type = FastPathUpdateType::Bitmap;
                    // The payload is a TS_UPDATE_BITMAP: a 2-byte updateType (0x0001) then the bitmap data.
                    Bytes body(payload.begin() + p, payload.begin() + p + size);
                    if (body.size() < 2)
                    {
                        error = "a fast-path bitmap update was too short";
                        return false;
                    }
                    Bytes afterType(body.begin() + 2, body.end());
                    if (!ParseBitmapUpdate(afterType, update.rects, error))
                    {
                        return false;
                    }
                }
                else if (code == 0x04 || code == 0x05 || code == 0x06 || code == 0x08) // pointer updates
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
