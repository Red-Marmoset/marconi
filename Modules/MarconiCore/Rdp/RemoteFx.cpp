// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RLGR1, the two inverse wavelets and YCbCr to RGB, shared by the RemoteFX codecs.

#include <MarconiCore/Rdp/RemoteFx.h>

#include <algorithm>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            int16_t Clamp16(int value)
            {
                return static_cast<int16_t>(std::clamp(value, -32768, 32767));
            }

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
        } // namespace

        void RemoteFxInverseWavelet(int16_t* buffer, bool extrapolate)
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

    void RemoteFxYCbCrToRgb(const int16_t* y, const int16_t* cb, const int16_t* cr, uint32_t* out)
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

        void DecodeRlgr1(const uint8_t* data, size_t size, int16_t* out, size_t count)
        {
            std::fill(out, out + count, static_cast<int16_t>(0));
            RemoteFxBitReader bits(data, size);
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
    } // namespace Rdp
} // namespace Marconi
