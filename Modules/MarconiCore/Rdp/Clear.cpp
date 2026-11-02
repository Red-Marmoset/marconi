// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ClearCodec decoding: glyphs, residual, bands and subcodecs, in the order MS-RDPEGFX 3.3.8.1 gives.

#include <MarconiCore/Rdp/Clear.h>

#include <MarconiCore/Rdp/Nsc.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr uint8_t kGlyphIndex = 0x01;
            constexpr uint8_t kGlyphHit = 0x02;
            constexpr uint8_t kCacheReset = 0x04;
            constexpr size_t kBarCount = 32768;
            constexpr size_t kShortBarCount = 16384;
            constexpr size_t kGlyphCount = 4000;
            constexpr uint32_t kMaxBarHeight = 52;

            uint16_t GetU16(const uint8_t* p)
            {
                return static_cast<uint16_t>(p[0] | (p[1] << 8));
            }
            uint32_t GetU32(const uint8_t* p)
            {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }
            uint32_t Bgr(const uint8_t* p)
            {
                return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
            }

            // Reads a run length: a byte, or 0xFF then two bytes, or 0xFFFF then four.
            bool RunLength(const uint8_t* data, size_t size, size_t& p, uint32_t first, uint32_t& run)
            {
                run = first;
                if (run < 0xFF)
                {
                    return true;
                }
                if (size - p < 2)
                {
                    return false;
                }
                run = GetU16(data + p);
                p += 2;
                if (run < 0xFFFF)
                {
                    return true;
                }
                if (size - p < 4)
                {
                    return false;
                }
                run = GetU32(data + p);
                p += 4;
                return true;
            }

            // Copies `width` by `height` pixels (`stride` apart) to the surface at x,y, trimmed to its right and
            // bottom edges.
            void CopyClipped(const uint32_t* src, int stride, int width, int height, uint32_t* pixels,
                             int surfaceWidth, int surfaceHeight, int x, int y)
            {
                width = std::min(width, surfaceWidth - x);
                height = std::min(height, surfaceHeight - y);
                for (int row = 0; row < height; ++row)
                {
                    std::memcpy(pixels + static_cast<size_t>(y + row) * surfaceWidth + x,
                                src + static_cast<size_t>(row) * stride, sizeof(uint32_t) * std::max(width, 0));
                }
            }

            // The palette run-length subcodec (RLEX, MS-RDPEGFX 2.2.4.1.1.3.1.1).
            bool DecodeRlex(const uint8_t* data, size_t size, int width, int height, std::vector<uint32_t>& out)
            {
                if (size < 1)
                {
                    return false;
                }
                uint32_t paletteCount = data[0];
                if (paletteCount < 1 || paletteCount > 127 || size < 1 + paletteCount * 3)
                {
                    return false;
                }
                uint32_t palette[128] = {};
                for (uint32_t i = 0; i < paletteCount; ++i)
                {
                    palette[i] = Bgr(data + 1 + i * 3);
                }
                // Bits for a palette index: floor(log2(count - 1)) + 1.
                uint32_t numBits = 1;
                while (numBits < 8 && (1u << numBits) < paletteCount)
                {
                    ++numBits;
                }
                if (paletteCount == 1)
                {
                    numBits = 1;
                }
                const size_t pixelCount = static_cast<size_t>(width) * height;
                out.assign(pixelCount, 0);
                size_t index = 0;
                size_t p = 1 + paletteCount * 3;
                while (p < size)
                {
                    if (size - p < 2)
                    {
                        return false;
                    }
                    uint8_t packed = data[p];
                    uint32_t run = 0;
                    uint32_t first = data[p + 1];
                    p += 2;
                    if (!RunLength(data, size, p, first, run))
                    {
                        return false;
                    }
                    uint8_t suiteDepth = static_cast<uint8_t>((packed >> numBits) & ((1u << (8 - numBits)) - 1));
                    uint8_t stopIndex = static_cast<uint8_t>(packed & ((1u << numBits) - 1));
                    uint8_t startIndex = static_cast<uint8_t>(stopIndex - suiteDepth);
                    if (startIndex >= paletteCount || stopIndex >= paletteCount)
                    {
                        return false;
                    }
                    // A run of the start colour, then the suite start..stop, one pixel each.
                    if (index + run > pixelCount)
                    {
                        return false;
                    }
                    std::fill(out.begin() + index, out.begin() + index + run, palette[startIndex]);
                    index += run;
                    if (index + suiteDepth + 1 > pixelCount)
                    {
                        return false;
                    }
                    for (uint32_t i = 0; i <= suiteDepth; ++i)
                    {
                        out[index++] = palette[startIndex + i];
                    }
                }
                return index == pixelCount;
            }
        } // namespace

        void ClearDecoder::Bar::Fit()
        {
            if (pixels.size() < count)
            {
                pixels.resize(count, 0);
            }
        }

        ClearDecoder::ClearDecoder() : m_bars(kBarCount), m_shortBars(kShortBarCount), m_glyphs(kGlyphCount)
        {
        }

        bool ClearDecoder::Residual(const uint8_t* data, size_t size, const Target& target, std::string& error)
        {
            const size_t pixelCount = static_cast<size_t>(target.width) * target.height;
            std::vector<uint32_t> pixels(pixelCount, 0);
            size_t index = 0;
            size_t p = 0;
            while (p < size)
            {
                if (size - p < 4)
                {
                    error = "a ClearCodec residual run is cut short";
                    return false;
                }
                uint32_t colour = Bgr(data + p);
                uint32_t first = data[p + 3];
                p += 4;
                uint32_t run = 0;
                if (!RunLength(data, size, p, first, run) || index >= pixelCount || run > pixelCount - index)
                {
                    error = "a ClearCodec residual run doesn't fit its rectangle";
                    return false;
                }
                std::fill(pixels.begin() + index, pixels.begin() + index + run, colour);
                index += run;
            }
            if (index != pixelCount)
            {
                error = "the ClearCodec residual layer doesn't cover its rectangle";
                return false;
            }
            CopyClipped(pixels.data(), target.width, target.width, target.height, target.pixels,
                        target.surfaceWidth, target.surfaceHeight, target.x, target.y);
            return true;
        }

        bool ClearDecoder::Bands(const uint8_t* data, size_t size, const Target& target, std::string& error)
        {
            size_t p = 0;
            while (p < size)
            {
                if (size - p < 11)
                {
                    error = "a ClearCodec band header is cut short";
                    return false;
                }
                uint16_t xStart = GetU16(data + p);
                uint16_t xEnd = GetU16(data + p + 2);
                uint16_t yStart = GetU16(data + p + 4);
                uint16_t yEnd = GetU16(data + p + 6);
                uint32_t background = Bgr(data + p + 8);
                p += 11;
                if (xEnd < xStart || yEnd < yStart)
                {
                    error = "a ClearCodec band that ends before it starts";
                    return false;
                }
                const uint32_t barHeight = static_cast<uint32_t>(yEnd - yStart) + 1;
                if (barHeight > kMaxBarHeight)
                {
                    error = "a ClearCodec band taller than the format allows";
                    return false;
                }
                const uint32_t barCount = static_cast<uint32_t>(xEnd - xStart) + 1;
                for (uint32_t i = 0; i < barCount; ++i)
                {
                    if (size - p < 2)
                    {
                        error = "a ClearCodec band is cut short";
                        return false;
                    }
                    uint16_t header = GetU16(data + p);
                    p += 2;
                    Bar* bar = nullptr;
                    const Bar* shortBar = nullptr;
                    uint32_t yOn = 0;
                    bool update = false;
                    if ((header & 0xC000) == 0x4000) // a short bar from the cache
                    {
                        if (size - p < 1)
                        {
                            error = "a ClearCodec band is cut short";
                            return false;
                        }
                        shortBar = &m_shortBars[header & 0x3FFF];
                        yOn = data[p++];
                        update = true;
                    }
                    else if ((header & 0xC000) == 0x0000) // a new short bar
                    {
                        yOn = header & 0xFF;
                        uint32_t yOff = (header >> 8) & 0x3F;
                        if (yOff < yOn || yOff - yOn > kMaxBarHeight)
                        {
                            error = "a ClearCodec short bar of an impossible length";
                            return false;
                        }
                        uint32_t count = yOff - yOn;
                        if (size - p < count * 3)
                        {
                            error = "a ClearCodec band is cut short";
                            return false;
                        }
                        Bar& entry = m_shortBars[m_shortBarCursor];
                        entry.count = count;
                        entry.Fit();
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            entry.pixels[k] = Bgr(data + p + k * 3);
                        }
                        p += count * 3;
                        m_shortBarCursor = (m_shortBarCursor + 1) % kShortBarCount;
                        shortBar = &entry;
                        update = true;
                    }
                    else if ((header & 0x8000) == 0x8000) // a whole bar from the cache
                    {
                        bar = &m_bars[header & 0x7FFF];
                        if (bar->pixels.empty())
                        {
                            bar->count = barHeight; // a reset cache: black, rather than nothing
                            bar->Fit();
                        }
                    }
                    if (update)
                    {
                        // A new whole bar: background above yOn, the short bar, background below; it is cached.
                        bar = &m_bars[m_barCursor];
                        bar->count = barHeight;
                        bar->Fit();
                        uint32_t shortCount = shortBar->count;
                        for (uint32_t k = 0; k < barHeight; ++k)
                        {
                            bool inShort = k >= yOn && k - yOn < shortCount;
                            bar->pixels[k] = inShort ? shortBar->pixels[k - yOn] : background;
                        }
                        m_barCursor = (m_barCursor + 1) % kBarCount;
                    }
                    if (bar == nullptr)
                    {
                        error = "a ClearCodec bar with an unknown header";
                        return false;
                    }
                    if (bar->count != barHeight)
                    {
                        bar->count = barHeight;
                        bar->Fit();
                    }
                    // Drawn down from yStart in column xStart + i, while inside the rectangle and the surface.
                    if (i < static_cast<uint32_t>(target.width))
                    {
                        int column = target.x + xStart + static_cast<int>(i);
                        uint32_t count = std::min<uint32_t>(bar->count, static_cast<uint32_t>(target.height));
                        if (column >= target.surfaceWidth)
                        {
                            error = "a ClearCodec band runs off the surface";
                            return false;
                        }
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            int row = target.y + yStart + static_cast<int>(k);
                            if (row >= target.surfaceHeight)
                            {
                                error = "a ClearCodec band runs off the surface";
                                return false;
                            }
                            target.pixels[static_cast<size_t>(row) * target.surfaceWidth + column] = bar->pixels[k];
                        }
                    }
                }
            }
            return true;
        }

        bool ClearDecoder::Subcodecs(const uint8_t* data, size_t size, const Target& target, std::string& error)
        {
            size_t p = 0;
            while (p < size)
            {
                if (size - p < 13)
                {
                    error = "a ClearCodec subcodec header is cut short";
                    return false;
                }
                int xStart = GetU16(data + p);
                int yStart = GetU16(data + p + 2);
                int width = GetU16(data + p + 4);
                int height = GetU16(data + p + 6);
                uint32_t length = GetU32(data + p + 8);
                uint8_t codec = data[p + 12];
                p += 13;
                if (length > size - p)
                {
                    error = "a ClearCodec subcodec rectangle is cut short";
                    return false;
                }
                if (target.x + xStart + width > target.surfaceWidth || target.y + yStart + height > target.surfaceHeight ||
                    xStart + width > target.width || yStart + height > target.height)
                {
                    error = "a ClearCodec subcodec rectangle outside its area";
                    return false;
                }
                const uint8_t* body = data + p;
                std::vector<uint32_t> pixels;
                switch (codec)
                {
                    case 0: // uncompressed, B, G, R
                    {
                        if (length != static_cast<size_t>(width) * height * 3)
                        {
                            error = "a ClearCodec uncompressed rectangle of the wrong length";
                            return false;
                        }
                        pixels.resize(static_cast<size_t>(width) * height);
                        for (size_t k = 0; k < pixels.size(); ++k)
                        {
                            pixels[k] = Bgr(body + k * 3);
                        }
                        break;
                    }
                    case 1:
                        if (width == 0 || height == 0)
                        {
                            break; // nothing to draw (and NSCodec has no empty pictures)
                        }
                        if (!DecodeNsc(body, length, width, height, pixels, error))
                        {
                            error = "a ClearCodec NSCodec rectangle: " + error;
                            return false;
                        }
                        break;
                    case 2:
                        if (!DecodeRlex(body, length, width, height, pixels))
                        {
                            error = "a ClearCodec palette rectangle doesn't decode";
                            return false;
                        }
                        break;
                    default:
                        error = "a ClearCodec subcodec Marconi doesn't know";
                        return false;
                }
                if (!pixels.empty())
                {
                    CopyClipped(pixels.data(), width, width, height, target.pixels, target.surfaceWidth,
                                target.surfaceHeight, target.x + xStart, target.y + yStart);
                }
                p += length;
            }
            return true;
        }

        bool ClearDecoder::Decode(const uint8_t* data, size_t size, uint32_t* pixels, int surfaceWidth,
                                  int surfaceHeight, int x, int y, int width, int height, std::string& error)
        {
            if (width < 0 || height < 0 || width > 0xFFFF || height > 0xFFFF || x < 0 || y < 0 || x > surfaceWidth ||
                y > surfaceHeight || surfaceWidth <= 0 || surfaceHeight <= 0)
            {
                error = "a ClearCodec picture outside its surface";
                return false;
            }
            if (size < 2)
            {
                error = "a ClearCodec picture too short";
                return false;
            }
            uint8_t flags = data[0];
            uint8_t sequence = data[1];
            if (m_sequence == 0 && sequence != 0)
            {
                m_sequence = sequence;
            }
            if (sequence != m_sequence)
            {
                error = "a ClearCodec picture out of sequence";
                return false;
            }
            m_sequence = static_cast<uint8_t>(sequence + 1);
            if (flags & kCacheReset)
            {
                m_barCursor = 0;
                m_shortBarCursor = 0;
            }
            size_t p = 2;
            Glyph* storeGlyph = nullptr;
            if ((flags & kGlyphHit) && !(flags & kGlyphIndex))
            {
                error = "a ClearCodec glyph hit without a glyph index";
                return false;
            }
            if (flags & kGlyphIndex)
            {
                if (static_cast<size_t>(width) * height > 1024 * 1024 || size - p < 2)
                {
                    error = "a ClearCodec glyph that doesn't fit the format";
                    return false;
                }
                uint16_t index = GetU16(data + p);
                p += 2;
                if (index >= kGlyphCount)
                {
                    error = "a ClearCodec glyph index outside the cache";
                    return false;
                }
                Glyph& glyph = m_glyphs[index];
                if (flags & kGlyphHit)
                {
                    if (glyph.pixels.empty() || static_cast<size_t>(width) * height > glyph.count)
                    {
                        error = "a ClearCodec glyph hit on a glyph that isn't cached";
                        return false;
                    }
                    CopyClipped(glyph.pixels.data(), width, width, height, pixels, surfaceWidth, surfaceHeight, x, y);
                }
                else
                {
                    size_t count = static_cast<size_t>(width) * height;
                    if (count == 0)
                    {
                        error = "an empty ClearCodec glyph";
                        return false;
                    }
                    if (count > glyph.pixels.size())
                    {
                        glyph.pixels.resize(count, 0);
                        glyph.count = static_cast<uint32_t>(count);
                    }
                    storeGlyph = &glyph;
                }
            }
            if (size - p < 12)
            {
                if ((flags & (kGlyphHit | kGlyphIndex)) == (kGlyphHit | kGlyphIndex))
                {
                    return true; // a cached glyph and nothing more
                }
                error = "a ClearCodec picture too short for its layers";
                return false;
            }
            uint32_t residualBytes = GetU32(data + p);
            uint32_t bandsBytes = GetU32(data + p + 4);
            uint32_t subcodecBytes = GetU32(data + p + 8);
            p += 12;
            Target target{pixels, surfaceWidth, surfaceHeight, x, y, width, height};
            if (residualBytes > size - p || !(residualBytes == 0 || Residual(data + p, residualBytes, target, error)))
            {
                if (error.empty())
                {
                    error = "a ClearCodec residual layer cut short";
                }
                return false;
            }
            p += residualBytes;
            if (bandsBytes > size - p || !(bandsBytes == 0 || Bands(data + p, bandsBytes, target, error)))
            {
                if (error.empty())
                {
                    error = "a ClearCodec bands layer cut short";
                }
                return false;
            }
            p += bandsBytes;
            if (subcodecBytes > size - p || !(subcodecBytes == 0 || Subcodecs(data + p, subcodecBytes, target, error)))
            {
                if (error.empty())
                {
                    error = "a ClearCodec subcodec layer cut short";
                }
                return false;
            }
            if (storeGlyph != nullptr)
            {
                // The finished picture becomes the glyph (as much of it as is on the surface).
                int w = std::min(width, surfaceWidth - x);
                int h = std::min(height, surfaceHeight - y);
                for (int row = 0; row < h; ++row)
                {
                    std::memcpy(storeGlyph->pixels.data() + static_cast<size_t>(row) * w,
                                pixels + static_cast<size_t>(y + row) * surfaceWidth + x, sizeof(uint32_t) * w);
                }
            }
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
