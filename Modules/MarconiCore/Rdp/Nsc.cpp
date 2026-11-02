// Copyright (c) 2026 Red Marmoset Studios Ltd.
// NSCodec decoding: the planes' run-length coding, then YCoCg to RGB.

#include <MarconiCore/Rdp/Nsc.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr int kMaxSide = 16384;

            uint32_t GetU32(const uint8_t* p)
            {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }

            // MS-RDPNSC 2.2.2: a value, and when the next byte repeats it, a run length (one byte plus 2, or 0xFF
            // then four bytes); the last four bytes of the plane are always stored as they are.
            bool DecodeRle(const uint8_t* in, size_t inSize, uint8_t* out, size_t originalSize)
            {
                size_t left = originalSize;
                while (left > 4)
                {
                    if (inSize < 1)
                    {
                        return false;
                    }
                    uint8_t value = *in++;
                    --inSize;
                    if (left == 5)
                    {
                        *out++ = value;
                        --left;
                    }
                    else if (inSize < 1)
                    {
                        return false;
                    }
                    else if (value == *in)
                    {
                        ++in;
                        --inSize;
                        if (inSize < 1)
                        {
                            return false;
                        }
                        size_t length = 0;
                        if (*in < 0xFF)
                        {
                            length = static_cast<size_t>(*in++) + 2;
                            --inSize;
                        }
                        else
                        {
                            if (inSize < 5)
                            {
                                return false;
                            }
                            length = GetU32(in + 1);
                            in += 5;
                            inSize -= 5;
                        }
                        if (length > left)
                        {
                            return false;
                        }
                        std::memset(out, value, length);
                        out += length;
                        left -= length;
                    }
                    else
                    {
                        *out++ = value;
                        --left;
                    }
                }
                if (left < 4 || inSize < 4)
                {
                    return false;
                }
                std::memcpy(out, in, 4);
                return true;
            }
        } // namespace

        bool DecodeNsc(const uint8_t* data, size_t size, int width, int height, std::vector<uint32_t>& out,
                       std::string& error)
        {
            if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide)
            {
                error = "an NSCodec bitmap of an impossible size";
                return false;
            }
            if (size < 20)
            {
                error = "an NSCodec bitmap too short for its header";
                return false;
            }
            uint32_t planeBytes[4];
            size_t total = 0;
            for (int i = 0; i < 4; ++i)
            {
                planeBytes[i] = GetU32(data + i * 4);
                total += planeBytes[i];
            }
            int colorLoss = data[16];
            bool subsampled = data[17] != 0;
            if (colorLoss < 1 || colorLoss > 7)
            {
                error = "an NSCodec bitmap with a colour loss level outside the format";
                return false;
            }
            if (total > size - 20)
            {
                error = "an NSCodec bitmap shorter than its planes";
                return false;
            }
            // Planes are as wide as the picture, or with half-size chroma, luma padded to a multiple of 8 and
            // chroma half that by half the height rounded up to even.
            const size_t paddedWidth = (static_cast<size_t>(width) + 7) & ~static_cast<size_t>(7);
            const size_t paddedHeight = (static_cast<size_t>(height) + 1) & ~static_cast<size_t>(1);
            size_t original[4];
            for (size_t& count : original)
            {
                count = static_cast<size_t>(width) * height;
            }
            if (subsampled)
            {
                original[0] = paddedWidth * height;
                original[1] = (paddedWidth / 2) * (paddedHeight / 2);
                original[2] = original[1];
            }
            std::vector<uint8_t> planes[4];
            const uint8_t* p = data + 20;
            for (int i = 0; i < 4; ++i)
            {
                planes[i].assign(std::max(original[i], paddedWidth * paddedHeight), 0);
                if (planeBytes[i] == 0)
                {
                    std::fill(planes[i].begin(), planes[i].begin() + original[i], 0xFF);
                }
                else if (planeBytes[i] < original[i])
                {
                    // Bounded by what is left of the planes, not this plane's count, as FreeRDP's (and so the
                    // servers built on it) read it.
                    size_t left = static_cast<size_t>(data + 20 + total - p);
                    if (!DecodeRle(p, left, planes[i].data(), original[i]))
                    {
                        error = "an NSCodec plane's run-length coding doesn't add up";
                        return false;
                    }
                }
                else
                {
                    std::memcpy(planes[i].data(), p, original[i]);
                }
                p += planeBytes[i];
            }
            const int shift = colorLoss - 1;
            out.assign(static_cast<size_t>(width) * height, 0);
            for (int y = 0; y < height; ++y)
            {
                const uint8_t* luma = planes[0].data() + static_cast<size_t>(y) * (subsampled ? paddedWidth : width);
                size_t chromaRow = subsampled ? static_cast<size_t>(y >> 1) * (paddedWidth >> 1)
                                              : static_cast<size_t>(y) * width;
                for (int x = 0; x < width; ++x)
                {
                    size_t chroma = chromaRow + (subsampled ? static_cast<size_t>(x >> 1) : static_cast<size_t>(x));
                    int yValue = luma[x];
                    int co = static_cast<int8_t>(static_cast<uint8_t>(planes[1][chroma] << shift));
                    int cg = static_cast<int8_t>(static_cast<uint8_t>(planes[2][chroma] << shift));
                    uint32_t r = static_cast<uint32_t>(std::clamp(yValue + co - cg, 0, 255));
                    uint32_t g = static_cast<uint32_t>(std::clamp(yValue + cg, 0, 255));
                    uint32_t b = static_cast<uint32_t>(std::clamp(yValue - co - cg, 0, 255));
                    out[static_cast<size_t>(y) * width + x] = (r << 16) | (g << 8) | b;
                }
            }
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
