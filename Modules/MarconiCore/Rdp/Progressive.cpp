// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The RemoteFX Progressive decoder (MS-RDPEGFX 2.2.4.2 and 3.2.8.1, MS-RDPRFX 3.1.8.1): blocks, tiles, RLGR1, the
// refinement passes, both inverse wavelets, and YCbCr to RGB.

#include <MarconiCore/Rdp/Progressive.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            enum : uint16_t
            {
                kBlockSync = 0xCCC0,
                kBlockFrameBegin = 0xCCC1,
                kBlockFrameEnd = 0xCCC2,
                kBlockContext = 0xCCC3,
                kBlockRegion = 0xCCC4,
                kBlockTileSimple = 0xCCC5,
                kBlockTileFirst = 0xCCC6,
                kBlockTileUpgrade = 0xCCC7,
            };
            constexpr uint8_t kContextSubbandDiffing = 0x01;
            constexpr uint8_t kRegionReduceExtrapolate = 0x01;
            constexpr uint8_t kTileDifference = 0x01;
            constexpr uint8_t kFullQuality = 0xFF;

            int16_t Clamp16(int value)
            {
                return static_cast<int16_t>(std::clamp(value, -32768, 32767));
            }

            uint16_t GetU16(const uint8_t* p)
            {
                return static_cast<uint16_t>(p[0] | (p[1] << 8));
            }
            uint32_t GetU32(const uint8_t* p)
            {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }

            // Bits most significant first; reading past the end gives zeros, as the format's decoders expect.
            class BitReader
            {
            public:
                BitReader(const uint8_t* data, size_t size) : m_data(data), m_bits(size * 8) {}

                size_t Remaining() const { return m_pos < m_bits ? m_bits - m_pos : 0; }
                uint32_t Peek() const
                {
                    return m_pos < m_bits ? (m_data[m_pos >> 3] >> (7 - (m_pos & 7))) & 1 : 0;
                }
                uint32_t Bit()
                {
                    uint32_t bit = Peek();
                    ++m_pos;
                    return bit;
                }
                uint32_t Bits(uint32_t count)
                {
                    uint32_t value = 0;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        value = (value << 1) | Bit();
                    }
                    return value;
                }
                // Counts and consumes a run of `bit` values, stopping at the end of the data.
                uint32_t CountRun(uint32_t bit)
                {
                    uint32_t count = 0;
                    while (Remaining() > 0 && Peek() == bit)
                    {
                        ++m_pos;
                        ++count;
                    }
                    return count;
                }

            private:
                const uint8_t* m_data;
                size_t m_bits;
                size_t m_pos = 0;
            };

            // RLGR parameters (MS-RDPRFX 3.1.8.1.7.3).
            constexpr uint32_t kKpMax = 80;
            constexpr uint32_t kLsgr = 3;
            constexpr uint32_t kUpGr = 4;
            constexpr uint32_t kDnGr = 6;
            constexpr uint32_t kUqGr = 3;
            constexpr uint32_t kDqGr = 3;

            void UpdateKr(uint32_t vk, uint32_t& krp, uint32_t& kr)
            {
                if (vk == 0)
                {
                    krp = krp > 2 ? krp - 2 : 0;
                }
                else if (vk != 1)
                {
                    krp = std::min(krp + vk, kKpMax);
                }
                kr = krp >> kLsgr;
            }

            // The ten bands of a 64x64 tile's coefficients, in buffer order, for each wavelet layout.
            struct Band
            {
                size_t offset;
                size_t count;
                int ProgressiveQuant::*quant;
            };
            const Band kClassicBands[10] = {
                {0, 1024, &ProgressiveQuant::hl1},    {1024, 1024, &ProgressiveQuant::lh1},
                {2048, 1024, &ProgressiveQuant::hh1}, {3072, 256, &ProgressiveQuant::hl2},
                {3328, 256, &ProgressiveQuant::lh2},  {3584, 256, &ProgressiveQuant::hh2},
                {3840, 64, &ProgressiveQuant::hl3},   {3904, 64, &ProgressiveQuant::lh3},
                {3968, 64, &ProgressiveQuant::hh3},   {4032, 64, &ProgressiveQuant::ll3},
            };
            const Band kExtrapolateBands[10] = {
                {0, 1023, &ProgressiveQuant::hl1},    {1023, 1023, &ProgressiveQuant::lh1},
                {2046, 961, &ProgressiveQuant::hh1},  {3007, 272, &ProgressiveQuant::hl2},
                {3279, 272, &ProgressiveQuant::lh2},  {3551, 256, &ProgressiveQuant::hh2},
                {3807, 72, &ProgressiveQuant::hl3},   {3879, 72, &ProgressiveQuant::lh3},
                {3951, 64, &ProgressiveQuant::hh3},   {4015, 81, &ProgressiveQuant::ll3},
            };

            // RFX_COMPONENT_CODEC_QUANT: five bytes, two 4-bit values each.
            ProgressiveQuant ReadQuant(const uint8_t* p)
            {
                ProgressiveQuant q;
                q.ll3 = p[0] & 0x0F;
                q.hl3 = p[0] >> 4;
                q.lh3 = p[1] & 0x0F;
                q.hh3 = p[1] >> 4;
                q.hl2 = p[2] & 0x0F;
                q.lh2 = p[2] >> 4;
                q.hh2 = p[3] & 0x0F;
                q.hl1 = p[3] >> 4;
                q.lh1 = p[4] & 0x0F;
                q.hh1 = p[4] >> 4;
                return q;
            }

            ProgressiveQuant Add(const ProgressiveQuant& a, const ProgressiveQuant& b, int extra = 0)
            {
                ProgressiveQuant r;
                for (const Band& band : kClassicBands)
                {
                    r.*band.quant = a.*band.quant + b.*band.quant + extra;
                }
                return r;
            }

            bool AllWithin(const ProgressiveQuant& q, int low, int high)
            {
                for (const Band& band : kClassicBands)
                {
                    if (q.*band.quant < low || q.*band.quant > high)
                    {
                        return false;
                    }
                }
                return true;
            }

            // One level of the classic RemoteFX inverse wavelet (MS-RDPRFX 3.1.8.1.4): the four sub-bands of
            // `width` by `width` at `buffer` (HL, LH, HH, LL) become one band twice the size, in place.
            void ClassicIdwtLevel(int16_t* buffer, int16_t* temp, size_t width)
            {
                const size_t total = width * 2;
                const int16_t* hl = buffer;
                const int16_t* lh = buffer + width * width;
                const int16_t* hh = buffer + width * width * 2;
                const int16_t* ll = buffer + width * width * 3;
                int16_t* lDst = temp;
                int16_t* hDst = temp + width * width * 2;
                for (size_t y = 0; y < width; ++y)
                {
                    lDst[0] = Clamp16(ll[0] - ((hl[0] + hl[0] + 1) >> 1));
                    hDst[0] = Clamp16(lh[0] - ((hh[0] + hh[0] + 1) >> 1));
                    for (size_t n = 1; n < width; ++n)
                    {
                        size_t x = n << 1;
                        lDst[x] = Clamp16(ll[n] - ((hl[n - 1] + hl[n] + 1) >> 1));
                        hDst[x] = Clamp16(lh[n] - ((hh[n - 1] + hh[n] + 1) >> 1));
                    }
                    size_t n = 0;
                    for (; n < width - 1; ++n)
                    {
                        size_t x = n << 1;
                        lDst[x + 1] = Clamp16((hl[n] << 1) + ((lDst[x] + lDst[x + 2]) >> 1));
                        hDst[x + 1] = Clamp16((hh[n] << 1) + ((hDst[x] + hDst[x + 2]) >> 1));
                    }
                    size_t x = n << 1;
                    lDst[x + 1] = Clamp16((hl[n] << 1) + lDst[x]);
                    hDst[x + 1] = Clamp16((hh[n] << 1) + hDst[x]);
                    hl += width;
                    lh += width;
                    hh += width;
                    ll += width;
                    lDst += total;
                    hDst += total;
                }
                for (size_t x = 0; x < total; ++x)
                {
                    const int16_t* l = temp + x;
                    const int16_t* h = temp + x + width * total;
                    int16_t* dst = buffer + x;
                    *dst = Clamp16(*l - ((*h * 2 + 1) >> 1));
                    for (size_t n = 1; n < width; ++n)
                    {
                        l += total;
                        h += total;
                        dst[2 * total] = Clamp16(*l - ((*(h - total) + *h + 1) >> 1));
                        dst[total] = Clamp16((*(h - total) << 1) + ((*dst + dst[2 * total]) >> 1));
                        dst += 2 * total;
                    }
                    dst[total] = Clamp16((*h << 1) + ((*dst * 2) >> 1));
                }
            }

            void ClassicIdwt(int16_t* buffer, int16_t* temp)
            {
                ClassicIdwtLevel(buffer + 3840, temp, 8);
                ClassicIdwtLevel(buffer + 3072, temp, 16);
                ClassicIdwtLevel(buffer, temp, 32);
            }

            // The "reduce-extrapolate" inverse wavelet Progressive uses when the region asks for it (MS-RDPEGFX
            // 3.2.8.1.2.2): bands of uneven sizes (33 low, 31 high at the first level).
            void ExtrapolateIdwtRows(const int16_t* low, size_t lowStep, const int16_t* high, size_t highStep,
                                     int16_t* dst, size_t dstStep, size_t lowCount, size_t highCount,
                                     size_t rows, bool vertical)
            {
                // Rows (vertical false) step along a row by 1 and between rows by the steps; columns the other way.
                for (size_t i = 0; i < rows; ++i)
                {
                    const size_t along = vertical ? highStep : 1;
                    const size_t alongLow = vertical ? lowStep : 1;
                    const size_t alongDst = vertical ? dstStep : 1;
                    const int16_t* pL = vertical ? low + i : low + i * lowStep;
                    const int16_t* pH = vertical ? high + i : high + i * highStep;
                    int16_t* pX = vertical ? dst + i : dst + i * dstStep;
                    int h0 = *pH;
                    pH += along;
                    int l0 = *pL;
                    pL += alongLow;
                    int x0 = Clamp16(l0 - h0);
                    int x2 = Clamp16(l0 - h0);
                    for (size_t j = 0; j + 1 < highCount; ++j)
                    {
                        int h1 = *pH;
                        pH += along;
                        l0 = *pL;
                        pL += alongLow;
                        x2 = Clamp16(l0 - ((h0 + h1) / 2));
                        int x1 = Clamp16(((x0 + x2) / 2) + (2 * h0));
                        pX[0] = static_cast<int16_t>(x0);
                        pX[alongDst] = static_cast<int16_t>(x1);
                        pX += 2 * alongDst;
                        x0 = x2;
                        h0 = h1;
                    }
                    if (lowCount <= highCount + 1)
                    {
                        if (lowCount <= highCount)
                        {
                            pX[0] = static_cast<int16_t>(x2);
                            pX[alongDst] = Clamp16(x2 + (2 * h0));
                        }
                        else
                        {
                            l0 = *pL;
                            x0 = Clamp16(l0 - h0);
                            pX[0] = static_cast<int16_t>(x2);
                            pX[alongDst] = Clamp16(((x0 + x2) / 2) + (2 * h0));
                            pX[2 * alongDst] = static_cast<int16_t>(x0);
                        }
                    }
                    else
                    {
                        l0 = *pL;
                        pL += alongLow;
                        x0 = Clamp16(l0 - (h0 / 2));
                        pX[0] = static_cast<int16_t>(x2);
                        pX[alongDst] = Clamp16(((x0 + x2) / 2) + (2 * h0));
                        pX[2 * alongDst] = static_cast<int16_t>(x0);
                        l0 = *pL;
                        pX[3 * alongDst] = Clamp16((x0 + l0) / 2);
                    }
                }
            }

            void ExtrapolateIdwtLevel(int16_t* buffer, int16_t* temp, size_t level)
            {
                const size_t bandL = (64 >> level) + 1;
                const size_t bandH = level == 1 ? (64 >> 1) - 1 : (64 + (1u << (level - 1))) >> level;
                const int16_t* hl = buffer;
                const int16_t* lh = hl + bandH * bandL;
                const int16_t* hh = lh + bandL * bandH;
                const int16_t* ll = hh + bandH * bandH;
                const size_t step = bandL + bandH;
                int16_t* l = temp;
                int16_t* h = temp + bandL * step;
                ExtrapolateIdwtRows(ll, bandL, hl, bandH, l, step, bandL, bandH, bandL, false);
                ExtrapolateIdwtRows(lh, bandL, hh, bandH, h, step, bandL, bandH, bandH, false);
                ExtrapolateIdwtRows(l, step, h, step, buffer, step, bandL, bandH, bandL + bandH, true);
            }

            void ExtrapolateIdwt(int16_t* buffer, int16_t* temp)
            {
                ExtrapolateIdwtLevel(buffer + 3807, temp, 3);
                ExtrapolateIdwtLevel(buffer + 3007, temp, 2);
                ExtrapolateIdwtLevel(buffer, temp, 1);
            }

            // RLGR1 for the refinement passes' sign-run-length stream (MS-RDPEGFX 3.2.8.1.3.1).
            struct SrlState
            {
                BitReader* bits;
                uint32_t kp = 8;
                uint32_t nz = 0;
                bool unaryNext = false;
            };

            int SrlRead(SrlState& state, uint32_t numBits)
            {
                if (state.nz > 0)
                {
                    --state.nz;
                    return 0;
                }
                uint32_t k = state.kp / 8;
                if (!state.unaryNext)
                {
                    if (state.bits->Bit() == 0)
                    {
                        // A full run of 1 << k zeros.
                        state.nz = (1u << k) - 1;
                        state.kp = std::min<uint32_t>(state.kp + 4, 80);
                        return 0;
                    }
                    state.nz = 0;
                    state.unaryNext = true;
                    if (k > 0)
                    {
                        state.nz = state.bits->Bits(k);
                    }
                    if (state.nz > 0)
                    {
                        --state.nz;
                        return 0;
                    }
                }
                state.unaryNext = false;
                uint32_t sign = state.bits->Bit();
                state.kp = state.kp < 6 ? 0 : state.kp - 6;
                if (numBits == 1)
                {
                    return sign ? -1 : 1;
                }
                uint32_t magnitude = 1;
                const uint32_t max = (1u << numBits) - 1;
                while (magnitude < max)
                {
                    if (state.bits->Bit() != 0)
                    {
                        break;
                    }
                    ++magnitude;
                }
                int value = static_cast<int>(std::min<uint32_t>(magnitude, 32767));
                return sign ? -value : value;
            }

            void YCbCrToRgb(const int16_t* y, const int16_t* cb, const int16_t* cr, uint32_t* out)
            {
                // MS-RDPRFX 3.1.8.1.3 in fixed point, as FreeRDP and Windows compute it: the coefficients carry 5
                // fractional bits and Y is offset by 128.
                for (int i = 0; i < 64 * 64; ++i)
                {
                    int64_t luma = static_cast<int64_t>(y[i] + 4096) << 16;
                    int64_t r = (luma + static_cast<int64_t>(cr[i]) * 91916) >> 21;
                    int64_t g = (luma - static_cast<int64_t>(cb[i]) * 22527 - static_cast<int64_t>(cr[i]) * 46819) >> 21;
                    int64_t b = (luma + static_cast<int64_t>(cb[i]) * 115992) >> 21;
                    uint32_t r8 = static_cast<uint32_t>(std::clamp<int64_t>(r, 0, 255));
                    uint32_t g8 = static_cast<uint32_t>(std::clamp<int64_t>(g, 0, 255));
                    uint32_t b8 = static_cast<uint32_t>(std::clamp<int64_t>(b, 0, 255));
                    out[i] = (r8 << 16) | (g8 << 8) | b8;
                }
            }

            struct Region
            {
                std::vector<ProgressiveRect> rects; // surface coordinates, right/bottom exclusive
                std::vector<ProgressiveQuant> quants;
                std::vector<std::array<ProgressiveQuant, 3>> progQuants; // by quality: Y, Cb, Cr
                bool extrapolate = false;
            };

            class Decoder
            {
            public:
                Decoder(ProgressiveSurface& surface, uint32_t* pixels, int width, int height, int stride,
                        std::vector<ProgressiveRect>& updated, std::string& error)
                    : m_surface(surface), m_pixels(pixels), m_width(width), m_height(height), m_stride(stride),
                      m_updated(updated), m_error(error)
                {
                }

                bool Run(const uint8_t* data, size_t size)
                {
                    size_t p = 0;
                    while (p < size)
                    {
                        if (size - p < 6)
                        {
                            return Fail("a Progressive block too short for its header");
                        }
                        uint16_t type = GetU16(data + p);
                        uint32_t length = GetU32(data + p + 2);
                        if (length < 6 || length > size - p)
                        {
                            return Fail("a Progressive block with a bad length");
                        }
                        if (!Block(type, data + p + 6, length - 6))
                        {
                            return false;
                        }
                        p += length;
                    }
                    return true;
                }

            private:
                bool Fail(const char* what)
                {
                    m_error = what;
                    return false;
                }

                bool Block(uint16_t type, const uint8_t* body, size_t size)
                {
                    switch (type)
                    {
                        case kBlockSync:
                            if (size != 6 || GetU32(body) != 0xCACCACCA || GetU16(body + 4) != 0x0100)
                            {
                                return Fail("a Progressive sync block that isn't one");
                            }
                            return true;
                        case kBlockContext:
                            if (size != 4 || GetU16(body + 1) != 64)
                            {
                                return Fail("a Progressive context with a tile size other than 64");
                            }
                            m_surface.subbandDiffing = (body[3] & kContextSubbandDiffing) != 0;
                            return true;
                        case kBlockFrameBegin:
                        case kBlockFrameEnd:
                            return true; // regions are drawn as they come
                        case kBlockRegion:
                            return RegionBlock(body, size);
                        default:
                            return Fail("an unknown Progressive block");
                    }
                }

                bool RegionBlock(const uint8_t* body, size_t size)
                {
                    if (size < 12)
                    {
                        return Fail("a Progressive region too short");
                    }
                    uint8_t tileSize = body[0];
                    uint16_t numRects = GetU16(body + 1);
                    uint8_t numQuant = body[3];
                    uint8_t numProgQuant = body[4];
                    uint8_t flags = body[5];
                    uint16_t numTiles = GetU16(body + 6);
                    uint32_t tileDataSize = GetU32(body + 8);
                    if (tileSize != 64 || numQuant > 7)
                    {
                        return Fail("a Progressive region with settings outside the format");
                    }
                    size_t need = 12 + numRects * 8ull + numQuant * 5ull + numProgQuant * 16ull + tileDataSize;
                    if (need > size)
                    {
                        return Fail("a Progressive region shorter than its contents");
                    }
                    Region region;
                    region.extrapolate = (flags & kRegionReduceExtrapolate) != 0;
                    size_t p = 12;
                    for (uint16_t i = 0; i < numRects; ++i, p += 8)
                    {
                        ProgressiveRect rect;
                        rect.left = GetU16(body + p);
                        rect.top = GetU16(body + p + 2);
                        rect.right = rect.left + GetU16(body + p + 4);
                        rect.bottom = rect.top + GetU16(body + p + 6);
                        region.rects.push_back(rect);
                    }
                    for (uint8_t i = 0; i < numQuant; ++i, p += 5)
                    {
                        ProgressiveQuant quant = ReadQuant(body + p);
                        if (!AllWithin(quant, 6, 15))
                        {
                            return Fail("a Progressive region with a quantisation outside the format");
                        }
                        region.quants.push_back(quant);
                    }
                    for (uint8_t i = 0; i < numProgQuant; ++i, p += 16)
                    {
                        // A quality byte, then the Y, Cb and Cr refinement quantisations.
                        region.progQuants.push_back(
                            {ReadQuant(body + p + 1), ReadQuant(body + p + 6), ReadQuant(body + p + 11)});
                    }
                    const uint8_t* tiles = body + p;
                    size_t q = 0;
                    uint16_t count = 0;
                    while (q < tileDataSize)
                    {
                        if (tileDataSize - q < 6)
                        {
                            return Fail("a Progressive tile too short for its header");
                        }
                        uint16_t type = GetU16(tiles + q);
                        uint32_t length = GetU32(tiles + q + 2);
                        if (length < 6 || length > tileDataSize - q)
                        {
                            return Fail("a Progressive tile with a bad length");
                        }
                        if (!TileBlock(region, type, tiles + q + 6, length - 6))
                        {
                            return false;
                        }
                        q += length;
                        ++count;
                    }
                    if (count != numTiles)
                    {
                        return Fail("a Progressive region with a different number of tiles than it declared");
                    }
                    return true;
                }

                bool TileBlock(const Region& region, uint16_t type, const uint8_t* body, size_t size)
                {
                    bool upgrade = type == kBlockTileUpgrade;
                    bool simple = type == kBlockTileSimple;
                    if (!upgrade && !simple && type != kBlockTileFirst)
                    {
                        return Fail("an unknown Progressive tile type");
                    }
                    size_t header = upgrade ? 20 : simple ? 16 : 17;
                    if (size < header)
                    {
                        return Fail("a Progressive tile too short");
                    }
                    uint8_t quantIdx[3] = {body[0], body[1], body[2]};
                    uint16_t xIdx = GetU16(body + 3);
                    uint16_t yIdx = GetU16(body + 5);
                    uint8_t tileFlags = upgrade ? 0 : body[7];
                    uint8_t quality = simple ? kFullQuality : body[upgrade ? 7 : 8];
                    for (uint8_t index : quantIdx)
                    {
                        if (index >= region.quants.size())
                        {
                            return Fail("a Progressive tile names a quantisation its region doesn't have");
                        }
                    }
                    if (quality != kFullQuality && quality >= region.progQuants.size())
                    {
                        return Fail("a Progressive tile names a quality its region doesn't have");
                    }
                    if (xIdx * 64 >= m_width || yIdx * 64 >= m_height)
                    {
                        return Fail("a Progressive tile outside its surface");
                    }
                    // The lengths of the component streams that follow the header.
                    size_t lengths[6] = {};
                    size_t streams = upgrade ? 6 : 4; // upgrade: srl and raw per component; else Y, Cb, Cr, tail
                    size_t total = header;
                    for (size_t i = 0; i < streams; ++i)
                    {
                        lengths[i] = GetU16(body + (upgrade ? 8 : simple ? 8 : 9) + i * 2);
                        total += lengths[i];
                    }
                    if (total != size)
                    {
                        return Fail("a Progressive tile whose parts don't add up to its length");
                    }
                    std::unique_ptr<ProgressiveSurface::Tile>& slot =
                        m_surface.tiles[static_cast<uint32_t>(yIdx) * 65536u + xIdx];
                    if (!slot)
                    {
                        slot = std::make_unique<ProgressiveSurface::Tile>();
                    }
                    ProgressiveSurface::Tile& tile = *slot;
                    static const ProgressiveQuant kNoRefinement;
                    const uint8_t* stream = body + header;
                    int16_t pixels[3][4096];
                    for (int c = 0; c < 3; ++c)
                    {
                        const ProgressiveQuant& quant = region.quants[quantIdx[c]];
                        const ProgressiveQuant& prog =
                            quality == kFullQuality ? kNoRefinement : region.progQuants[quality][c];
                        bool ok = false;
                        if (upgrade)
                        {
                            const uint8_t* srl = stream;
                            const uint8_t* raw = stream + lengths[c * 2];
                            ok = UpgradeComponent(tile, c, quant, prog, srl, lengths[c * 2], raw,
                                                  lengths[c * 2 + 1], region.extrapolate, pixels[c]);
                            stream += lengths[c * 2] + lengths[c * 2 + 1];
                        }
                        else
                        {
                            ok = FirstComponent(tile, c, quant, prog, stream, lengths[c],
                                                (tileFlags & kTileDifference) != 0, region.extrapolate,
                                                pixels[c]);
                            stream += lengths[c];
                        }
                        if (!ok)
                        {
                            return false;
                        }
                    }
                    tile.pass = upgrade ? tile.pass + 1 : 1;
                    uint32_t rgb[64 * 64];
                    YCbCrToRgb(pixels[0], pixels[1], pixels[2], rgb);
                    Draw(region, xIdx * 64, yIdx * 64, rgb);
                    return true;
                }

                // A tile's first (or only) pass for one component: RLGR1, dequantise, inverse wavelet.
                bool FirstComponent(ProgressiveSurface::Tile& tile, int c, const ProgressiveQuant& quant,
                                    const ProgressiveQuant& prog, const uint8_t* data, size_t size,
                                    bool difference, bool extrapolate, int16_t* out)
                {
                    int16_t* buffer = out;
                    DecodeRlgr1(data, size, buffer, 4096);
                    std::memcpy(tile.sign[c].data(), buffer, sizeof(int16_t) * 4096);
                    ProgressiveQuant shift = Add(quant, prog, -1);
                    const Band* bands = extrapolate ? kExtrapolateBands : kClassicBands;
                    // The LL3 band is coded as differences from the coefficient before.
                    const Band& ll = bands[9];
                    for (size_t i = ll.offset + 1; i < ll.offset + ll.count; ++i)
                    {
                        buffer[i] = Clamp16(buffer[i] + buffer[i - 1]);
                    }
                    for (int b = 0; b < 10; ++b)
                    {
                        int amount = shift.*bands[b].quant;
                        if (amount < 0)
                        {
                            return Fail("a Progressive tile with a quantisation below the format's");
                        }
                        for (size_t i = bands[b].offset; i < bands[b].offset + bands[b].count; ++i)
                        {
                            buffer[i] = static_cast<int16_t>(buffer[i] * (1 << amount));
                        }
                    }
                    if (difference)
                    {
                        for (size_t i = 0; i < 4096; ++i)
                        {
                            buffer[i] = Clamp16(buffer[i] + tile.current[c][i]);
                        }
                    }
                    std::memcpy(tile.current[c].data(), buffer, sizeof(int16_t) * 4096);
                    tile.bitPos[c] = Add(quant, prog);
                    InverseWavelet(buffer, extrapolate);
                    return true;
                }

                // A refinement pass for one component: more bits of each coefficient, from the raw stream where
                // its sign is known and the sign-run-length stream where it isn't yet.
                bool UpgradeComponent(ProgressiveSurface::Tile& tile, int c, const ProgressiveQuant& quant,
                                      const ProgressiveQuant& prog, const uint8_t* srlData, size_t srlSize,
                                      const uint8_t* rawData, size_t rawSize, bool extrapolate, int16_t* out)
                {
                    ProgressiveQuant bitPos = Add(quant, prog);
                    ProgressiveQuant shift = Add(quant, prog, -1);
                    BitReader srl(srlData, srlSize);
                    BitReader raw(rawData, rawSize);
                    SrlState state;
                    state.bits = &srl;
                    int16_t* current = tile.current[c].data();
                    int16_t* sign = tile.sign[c].data();
                    for (int b = 0; b < 10; ++b)
                    {
                        const Band& band = kExtrapolateBands[b];
                        int numBits = tile.bitPos[c].*band.quant - bitPos.*band.quant;
                        int amount = shift.*band.quant;
                        if (numBits < 0 || amount < 0)
                        {
                            return Fail("a Progressive refinement that goes backwards");
                        }
                        if (numBits == 0)
                        {
                            continue;
                        }
                        bool isLl = b == 9;
                        for (size_t i = band.offset; i < band.offset + band.count; ++i)
                        {
                            int input = 0;
                            if (isLl || sign[i] > 0)
                            {
                                input = static_cast<int>(raw.Bits(static_cast<uint32_t>(numBits)));
                            }
                            else if (sign[i] < 0)
                            {
                                input = -static_cast<int>(raw.Bits(static_cast<uint32_t>(numBits)));
                            }
                            else
                            {
                                input = SrlRead(state, static_cast<uint32_t>(numBits));
                                sign[i] = static_cast<int16_t>(input);
                            }
                            current[i] = Clamp16(current[i] + input * (1 << amount));
                        }
                    }
                    tile.bitPos[c] = bitPos;
                    std::memcpy(out, current, sizeof(int16_t) * 4096);
                    InverseWavelet(out, extrapolate);
                    return true;
                }

                void InverseWavelet(int16_t* buffer, bool extrapolate)
                {
                    int16_t temp[4096];
                    if (extrapolate)
                    {
                        ExtrapolateIdwt(buffer, temp);
                    }
                    else
                    {
                        ClassicIdwt(buffer, temp);
                    }
                }

                // Copies the parts of a decoded tile that fall inside the region's rectangles.
                void Draw(const Region& region, int tileX, int tileY, const uint32_t* rgb)
                {
                    for (const ProgressiveRect& rect : region.rects)
                    {
                        int left = std::max({rect.left, tileX, 0});
                        int top = std::max({rect.top, tileY, 0});
                        int right = std::min({rect.right, tileX + 64, m_width});
                        int bottom = std::min({rect.bottom, tileY + 64, m_height});
                        if (left >= right || top >= bottom)
                        {
                            continue;
                        }
                        for (int y = top; y < bottom; ++y)
                        {
                            std::memcpy(m_pixels + static_cast<size_t>(y) * m_stride + left,
                                        rgb + (y - tileY) * 64 + (left - tileX),
                                        sizeof(uint32_t) * static_cast<size_t>(right - left));
                        }
                        m_updated.push_back({left, top, right, bottom});
                    }
                }

                ProgressiveSurface& m_surface;
                uint32_t* m_pixels;
                int m_width;
                int m_height;
                int m_stride;
                std::vector<ProgressiveRect>& m_updated;
                std::string& m_error;
            };
        } // namespace

        void DecodeRlgr1(const uint8_t* data, size_t size, int16_t* out, size_t count)
        {
            std::fill(out, out + count, static_cast<int16_t>(0));
            BitReader bits(data, size);
            uint32_t k = 1;
            uint32_t kp = 1 << kLsgr;
            uint32_t kr = 1;
            uint32_t krp = 1 << kLsgr;
            size_t n = 0;
            while (bits.Remaining() > 0 && n < count)
            {
                if (k != 0)
                {
                    // Run-length mode: a run of zeros, then one nonzero value.
                    uint32_t vk = bits.CountRun(0);
                    if (bits.Remaining() < 1)
                    {
                        break;
                    }
                    bits.Bit();
                    size_t run = 0;
                    while (vk-- > 0)
                    {
                        run += static_cast<size_t>(1) << k;
                        kp = std::min(kp + kUpGr, kKpMax);
                        k = kp >> kLsgr;
                    }
                    if (bits.Remaining() < k)
                    {
                        break;
                    }
                    run += bits.Bits(k);
                    if (bits.Remaining() < 1)
                    {
                        break;
                    }
                    uint32_t sign = bits.Bit();
                    vk = bits.CountRun(1);
                    if (bits.Remaining() < 1)
                    {
                        break;
                    }
                    bits.Bit();
                    if (bits.Remaining() < kr)
                    {
                        break;
                    }
                    uint32_t code = bits.Bits(kr) | (vk << kr);
                    UpdateKr(vk, krp, kr);
                    kp = kp > kDnGr ? kp - kDnGr : 0;
                    k = kp >> kLsgr;
                    if (code + 1 > 32767)
                    {
                        break;
                    }
                    int magnitude = static_cast<int>(code) + 1;
                    n += std::min(run, count - n); // the zeros are already there
                    if (n < count)
                    {
                        out[n++] = static_cast<int16_t>(sign ? -magnitude : magnitude);
                    }
                }
                else
                {
                    // Golomb-Rice mode: one value.
                    uint32_t vk = bits.CountRun(1);
                    if (bits.Remaining() < 1)
                    {
                        break;
                    }
                    bits.Bit();
                    if (bits.Remaining() < kr)
                    {
                        break;
                    }
                    uint32_t code = bits.Bits(kr) | (vk << kr);
                    UpdateKr(vk, krp, kr);
                    int value = 0;
                    if (code == 0)
                    {
                        kp = std::min(kp + kUqGr, kKpMax);
                        k = kp >> kLsgr;
                    }
                    else
                    {
                        kp = kp > kDqGr ? kp - kDqGr : 0;
                        k = kp >> kLsgr;
                        if (code > 65535)
                        {
                            break;
                        }
                        // code = 2 * magnitude - sign
                        value = (code & 1) ? -static_cast<int>((code + 1) >> 1) : static_cast<int>(code >> 1);
                    }
                    out[n++] = static_cast<int16_t>(value);
                }
            }
        }

        bool DecodeProgressive(const uint8_t* data, size_t size, ProgressiveSurface& surface, uint32_t* pixels,
                               int width, int height, int stride, std::vector<ProgressiveRect>& updated,
                               std::string& error)
        {
            updated.clear();
            Decoder decoder(surface, pixels, width, height, stride, updated, error);
            return decoder.Run(data, size);
        }
    } // namespace Rdp
} // namespace Marconi
