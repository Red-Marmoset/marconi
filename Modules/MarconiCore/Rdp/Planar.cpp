// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The planar bitmap codec: planes, their run-length coding, and RGB or YCoCg to pixels.

#include <MarconiCore/Rdp/Planar.h>

#include <algorithm>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr uint8_t kColorLossMask = 0x07;
            constexpr uint8_t kChromaSubsampling = 0x08;
            constexpr uint8_t kRle = 0x10;
            constexpr uint8_t kNoAlpha = 0x20;
            constexpr int kMaxSide = 16384;

            // One run-length coded plane (MS-RDPEGDI 2.2.2.5.1.1): per row, control bytes each giving some raw
            // bytes and then a run repeating the last of them. The first row holds values; later rows hold
            // deltas from the row above (sign in the low bit). `used` gets the bytes read.
            bool DecodeRlePlane(const uint8_t* data, size_t size, int width, int height, std::vector<uint8_t>& plane,
                                size_t& used, std::string& error)
            {
                plane.assign(static_cast<size_t>(width) * height, 0);
                size_t p = 0;
                for (int y = 0; y < height; ++y)
                {
                    uint8_t* row = plane.data() + static_cast<size_t>(y) * width;
                    const uint8_t* above = y > 0 ? row - width : nullptr;
                    int value = 0; // the value (first row) or delta (later rows) a run repeats
                    int x = 0;
                    while (x < width)
                    {
                        if (p >= size)
                        {
                            error = "a planar plane ends in the middle of a row";
                            return false;
                        }
                        uint8_t control = data[p++];
                        int run = control & 0x0F;
                        int raw = control >> 4;
                        if (run == 1)
                        {
                            run = raw + 16;
                            raw = 0;
                        }
                        else if (run == 2)
                        {
                            run = raw + 32;
                            raw = 0;
                        }
                        if (x + raw + run > width)
                        {
                            error = "a planar plane has a row longer than the picture";
                            return false;
                        }
                        if (static_cast<size_t>(raw) > size - p)
                        {
                            error = "a planar plane ends in the middle of a row";
                            return false;
                        }
                        for (int i = 0; i < raw + run; ++i, ++x)
                        {
                            if (i < raw)
                            {
                                uint8_t byte = data[p++];
                                if (above == nullptr)
                                {
                                    value = byte;
                                }
                                else
                                {
                                    value = (byte & 1) ? -((byte >> 1) + 1) : (byte >> 1);
                                }
                            }
                            row[x] = static_cast<uint8_t>(above == nullptr ? value : above[x] + value);
                        }
                    }
                }
                used = p;
                return true;
            }

            // A plane in either form; `p` advances past it.
            bool ReadPlane(const uint8_t* data, size_t size, size_t& p, bool rle, int width, int height,
                           std::vector<uint8_t>& plane, std::string& error)
            {
                size_t bytes = static_cast<size_t>(width) * height;
                if (!rle)
                {
                    if (bytes > size - p)
                    {
                        error = "a planar bitmap shorter than its planes";
                        return false;
                    }
                    plane.assign(data + p, data + p + bytes);
                    p += bytes;
                    return true;
                }
                size_t used = 0;
                if (!DecodeRlePlane(data + p, size - p, width, height, plane, used, error))
                {
                    return false;
                }
                p += used;
                return true;
            }

            uint8_t Clip(int value)
            {
                return static_cast<uint8_t>(std::clamp(value, 0, 255));
            }
        } // namespace

        bool DecodePlanar(const uint8_t* data, size_t size, int width, int height, bool bottomUp,
                          std::vector<uint32_t>& out, std::string& error)
        {
            if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide)
            {
                error = "a planar bitmap of an impossible size";
                return false;
            }
            if (size < 1)
            {
                error = "an empty planar bitmap";
                return false;
            }
            uint8_t header = data[0];
            int colorLoss = header & kColorLossMask;
            bool subsampled = (header & kChromaSubsampling) != 0;
            bool rle = (header & kRle) != 0;
            bool alpha = (header & kNoAlpha) == 0;
            if (colorLoss == 0 && subsampled)
            {
                error = "a planar bitmap with half-size colour planes in RGB, which the format doesn't allow";
                return false;
            }
            int chromaWidth = subsampled ? (width + 1) / 2 : width;
            int chromaHeight = subsampled ? (height + 1) / 2 : height;
            size_t p = 1;
            std::vector<uint8_t> planes[4];
            // Alpha first when present, then red (or luma), green (or orange chroma), blue (or green chroma).
            if (alpha && !ReadPlane(data, size, p, rle, width, height, planes[3], error))
            {
                return false;
            }
            if (!ReadPlane(data, size, p, rle, width, height, planes[0], error) ||
                !ReadPlane(data, size, p, rle, chromaWidth, chromaHeight, planes[1], error) ||
                !ReadPlane(data, size, p, rle, chromaWidth, chromaHeight, planes[2], error))
            {
                return false;
            }
            // A raw bitmap may end with one pad byte; nothing else should follow.

            out.assign(static_cast<size_t>(width) * height, 0);
            for (int y = 0; y < height; ++y)
            {
                int outRow = bottomUp ? height - 1 - y : y;
                uint32_t* dst = out.data() + static_cast<size_t>(outRow) * width;
                const uint8_t* p0 = planes[0].data() + static_cast<size_t>(y) * width;
                for (int x = 0; x < width; ++x)
                {
                    size_t chroma = subsampled ? static_cast<size_t>(y / 2) * chromaWidth + x / 2
                                               : static_cast<size_t>(y) * width + x;
                    uint8_t a = planes[1][chroma];
                    uint8_t b = planes[2][chroma];
                    uint32_t r8, g8, b8;
                    if (colorLoss == 0)
                    {
                        r8 = p0[x];
                        g8 = a;
                        b8 = b;
                    }
                    else
                    {
                        // YCoCg (MS-RDPEGDI 3.1.9.1.2): the chroma planes were shifted right by the colour loss
                        // level; shifting back by one less also undoes the halving in the transform.
                        int shift = colorLoss - 1;
                        int co = static_cast<int8_t>(static_cast<uint8_t>(a << shift));
                        int cg = static_cast<int8_t>(static_cast<uint8_t>(b << shift));
                        int luma = p0[x];
                        int t = luma - cg;
                        r8 = Clip(t - co);
                        g8 = Clip(luma + cg);
                        b8 = Clip(t + co);
                    }
                    dst[x] = (r8 << 16) | (g8 << 8) | b8;
                }
            }
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
