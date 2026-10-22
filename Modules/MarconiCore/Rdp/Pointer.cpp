// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP pointer updates: the colour and "new" pointer shapes decoded to ARGB, and the smaller pointer messages.

#include <MarconiCore/Rdp/Pointer.h>

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
            uint32_t GetU32(const Bytes& data, size_t offset)
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
                }
                return value;
            }

            // Slow-path pointer message types (TS_POINTER_PDU messageType).
            enum : uint16_t
            {
                kPtrMsgSystem = 1,
                kPtrMsgPosition = 3,
                kPtrMsgColor = 6,
                kPtrMsgCached = 7,
                kPtrMsgPointer = 8,
                kPtrMsgLargePointer = 9,
            };
            // Fast-path update codes for pointers (MS-RDPBCGR 2.2.9.1.2.1).
            enum : uint8_t
            {
                kFastPtrNull = 5,
                kFastPtrDefault = 6,
                kFastPtrPosition = 8,
                kFastPtrColor = 9,
                kFastPtrCached = 10,
                kFastPtrPointer = 11,
                kFastPtrLarge = 12,
            };
            constexpr uint32_t kSysPtrNull = 0x00000000;
            constexpr uint32_t kSysPtrDefault = 0x00007F00;

            // Rows of a mask are padded to a 2-byte boundary.
            size_t RowBytes(int width, int bitsPerPixel)
            {
                size_t bytes = (static_cast<size_t>(width) * static_cast<size_t>(bitsPerPixel) + 7) / 8;
                return (bytes + 1) & ~static_cast<size_t>(1);
            }

            uint32_t GreyRamp(uint8_t index)
            {
                return (static_cast<uint32_t>(index) << 16) | (static_cast<uint32_t>(index) << 8) | index;
            }

            // The colour of pixel `x` in a row of the XOR mask, as 0x00RRGGBB, and its alpha for 32-bit masks.
            uint32_t XorPixel(const uint8_t* row, int x, int bitsPerPixel, const uint32_t* palette, uint8_t& alpha)
            {
                alpha = 255;
                switch (bitsPerPixel)
                {
                    case 1:
                    {
                        bool set = (row[x / 8] >> (7 - (x % 8))) & 1;
                        return set ? 0xFFFFFF : 0x000000;
                    }
                    case 8:
                        return palette != nullptr ? (palette[row[x]] & 0xFFFFFF) : GreyRamp(row[x]);
                    case 16:
                    {
                        uint16_t v = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
                        uint32_t r = (v >> 11) & 0x1F;
                        uint32_t g = (v >> 5) & 0x3F;
                        uint32_t b = v & 0x1F;
                        r = (r << 3) | (r >> 2);
                        g = (g << 2) | (g >> 4);
                        b = (b << 3) | (b >> 2);
                        return (r << 16) | (g << 8) | b;
                    }
                    case 24:
                        return (static_cast<uint32_t>(row[x * 3 + 2]) << 16) |
                               (static_cast<uint32_t>(row[x * 3 + 1]) << 8) | row[x * 3];
                    case 32:
                        alpha = row[x * 4 + 3];
                        return (static_cast<uint32_t>(row[x * 4 + 2]) << 16) |
                               (static_cast<uint32_t>(row[x * 4 + 1]) << 8) | row[x * 4];
                    default:
                        return 0;
                }
            }
        } // namespace

        bool DecodePointerAttribute(const Bytes& data, int xorBpp, const uint32_t* palette, PointerUpdate& out,
                                    std::string& error)
        {
            // TS_COLORPOINTERATTRIBUTE: cacheIndex, hotSpot (x, y), width, height, lengthAndMask, lengthXorMask,
            // xorMaskData, andMaskData. Both masks are stored bottom row first.
            if (data.size() < 14)
            {
                error = "a pointer shape was shorter than its header";
                return false;
            }
            if (xorBpp != 1 && xorBpp != 8 && xorBpp != 16 && xorBpp != 24 && xorBpp != 32)
            {
                error = "a pointer shape has a colour depth this build doesn't handle";
                return false;
            }
            out.cacheIndex = GetU16(data, 0);
            int hotspotX = GetU16(data, 2);
            int hotspotY = GetU16(data, 4);
            int width = GetU16(data, 6);
            int height = GetU16(data, 8);
            size_t lengthAnd = GetU16(data, 10);
            size_t lengthXor = GetU16(data, 12);
            if (width <= 0 || height <= 0 || width > 96 || height > 96)
            {
                error = "a pointer shape has an implausible size";
                return false;
            }
            size_t xorRow = RowBytes(width, xorBpp);
            size_t andRow = RowBytes(width, 1);
            if (14 + lengthXor + lengthAnd > data.size() || lengthXor < xorRow * static_cast<size_t>(height) ||
                lengthAnd < andRow * static_cast<size_t>(height))
            {
                error = "a pointer shape's masks don't cover its size";
                return false;
            }
            const uint8_t* xorMask = data.data() + 14;
            const uint8_t* andMask = xorMask + lengthXor;

            CursorShape& shape = out.shape;
            shape.width = width;
            shape.height = height;
            shape.hotspotX = hotspotX < width ? hotspotX : 0;
            shape.hotspotY = hotspotY < height ? hotspotY : 0;
            shape.argb.assign(static_cast<size_t>(width) * height, 0);
            // A 32-bit mask may carry real alpha; when every alpha is zero, the AND mask decides as usual.
            bool useAlpha = false;
            if (xorBpp == 32)
            {
                for (int y = 0; y < height && !useAlpha; ++y)
                {
                    const uint8_t* row = xorMask + static_cast<size_t>(y) * xorRow;
                    for (int x = 0; x < width; ++x)
                    {
                        if (row[x * 4 + 3] != 0)
                        {
                            useAlpha = true;
                            break;
                        }
                    }
                }
            }
            for (int y = 0; y < height; ++y)
            {
                // Row 0 of the shape is the last row of the masks.
                const uint8_t* xorRowData = xorMask + static_cast<size_t>(height - 1 - y) * xorRow;
                const uint8_t* andRowData = andMask + static_cast<size_t>(height - 1 - y) * andRow;
                uint32_t* dest = shape.argb.data() + static_cast<size_t>(y) * width;
                for (int x = 0; x < width; ++x)
                {
                    uint8_t alpha = 255;
                    uint32_t colour = XorPixel(xorRowData, x, xorBpp, palette, alpha);
                    bool andBit = (andRowData[x / 8] >> (7 - (x % 8))) & 1;
                    if (useAlpha)
                    {
                        dest[x] = (static_cast<uint32_t>(alpha >= 128 ? 255 : 0) << 24) | colour;
                    }
                    else if (andBit && colour == 0)
                    {
                        dest[x] = 0; // transparent
                    }
                    else if (andBit)
                    {
                        // "Invert the screen": not drawable as a cursor image; show it as black, which is what the
                        // outline of a typical arrow looks like on a light background.
                        dest[x] = 0xFF000000;
                    }
                    else
                    {
                        dest[x] = 0xFF000000 | colour;
                    }
                }
            }
            out.kind = PointerUpdateKind::New;
            return true;
        }

        namespace
        {
            bool ParseByKind(uint16_t kind, const Bytes& data, PointerUpdate& out, std::string& error)
            {
                switch (kind)
                {
                    case kPtrMsgSystem:
                        if (data.size() < 4)
                        {
                            error = "a system pointer update was too short";
                            return false;
                        }
                        out.kind = GetU32(data, 0) == kSysPtrNull ? PointerUpdateKind::Hidden
                                   : GetU32(data, 0) == kSysPtrDefault ? PointerUpdateKind::Default
                                                                        : PointerUpdateKind::Other;
                        return true;
                    case kPtrMsgPosition:
                        if (data.size() < 4)
                        {
                            error = "a pointer position update was too short";
                            return false;
                        }
                        out.kind = PointerUpdateKind::Position;
                        out.x = GetU16(data, 0);
                        out.y = GetU16(data, 2);
                        return true;
                    case kPtrMsgColor:
                        return DecodePointerAttribute(data, 24, nullptr, out, error);
                    case kPtrMsgCached:
                        if (data.size() < 2)
                        {
                            error = "a cached pointer update was too short";
                            return false;
                        }
                        out.kind = PointerUpdateKind::Cached;
                        out.cacheIndex = GetU16(data, 0);
                        return true;
                    case kPtrMsgPointer:
                    {
                        // TS_POINTERATTRIBUTE: xorBpp, then the colour pointer attribute at that depth.
                        if (data.size() < 2)
                        {
                            error = "a new pointer update was too short";
                            return false;
                        }
                        Bytes rest(data.begin() + 2, data.end());
                        return DecodePointerAttribute(rest, GetU16(data, 0), nullptr, out, error);
                    }
                    default:
                        out.kind = PointerUpdateKind::Other;
                        return true;
                }
            }
        } // namespace

        bool ParsePointerPdu(const Bytes& body, PointerUpdate& out, std::string& error)
        {
            if (body.size() < 4)
            {
                error = "a pointer update had no message type";
                return false;
            }
            Bytes data(body.begin() + 4, body.end()); // messageType (2), pad (2)
            return ParseByKind(GetU16(body, 0), data, out, error);
        }

        bool ParseFastPathPointer(uint8_t code, const Bytes& data, PointerUpdate& out, std::string& error)
        {
            switch (code)
            {
                case kFastPtrNull:
                    out.kind = PointerUpdateKind::Hidden;
                    return true;
                case kFastPtrDefault:
                    out.kind = PointerUpdateKind::Default;
                    return true;
                case kFastPtrPosition:
                    return ParseByKind(kPtrMsgPosition, data, out, error);
                case kFastPtrColor:
                    return ParseByKind(kPtrMsgColor, data, out, error);
                case kFastPtrCached:
                    return ParseByKind(kPtrMsgCached, data, out, error);
                case kFastPtrPointer:
                    return ParseByKind(kPtrMsgPointer, data, out, error);
                case kFastPtrLarge:
                    out.kind = PointerUpdateKind::Other;
                    return true;
                default:
                    error = "not a pointer update";
                    return false;
            }
        }

        CursorShape DefaultArrowCursor()
        {
            // A plain arrow, 12 by 19: '#' is black, '.' is white, ' ' is transparent.
            static const char* const kRows[19] = {
                "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ",
                "#.....#     ", "#......#    ", "#.......#   ", "#........#  ", "#.........# ", "#......#####",
                "#...#..#    ", "#..# #..#   ", "#.#  #..#   ", "##    #..#  ", "#     #..#  ", "       ##   ",
                "            ",
            };
            CursorShape shape;
            shape.width = 12;
            shape.height = 19;
            shape.hotspotX = 0;
            shape.hotspotY = 0;
            shape.argb.resize(static_cast<size_t>(shape.width) * shape.height);
            for (int y = 0; y < shape.height; ++y)
            {
                for (int x = 0; x < shape.width; ++x)
                {
                    char c = kRows[y][x];
                    shape.argb[static_cast<size_t>(y) * shape.width + x] =
                        c == '#' ? 0xFF000000 : c == '.' ? 0xFFFFFFFF : 0;
                }
            }
            return shape;
        }
    } // namespace Rdp
} // namespace Marconi
