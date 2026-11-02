// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The building blocks the RemoteFX codecs share (MS-RDPRFX 3.1.8.1): the RLGR1 entropy decoder, the classic and
// reduce-extrapolate inverse wavelets of a 64x64 tile, and YCbCr to RGB. Used by Rdp/Progressive.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Marconi
{
    namespace Rdp
    {
        // Bits most significant first; reading past the end gives zeros, as the format's decoders expect.
        class RemoteFxBitReader
        {
        public:
            RemoteFxBitReader(const uint8_t* data, size_t size) : m_data(data), m_bits(size * 8) {}

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

        // The RLGR1 entropy decoder RemoteFX uses (MS-RDPRFX 3.1.8.1.7.3): fills `out` (zeros past what the data
        // codes).
        void DecodeRlgr1(const uint8_t* data, size_t size, int16_t* out, size_t count);

        // The inverse wavelet of one 64x64 component, in place: the classic RemoteFX one (bands of 32, 16 and 8) or
        // Progressive's reduce-extrapolate one (bands of 33/31, 17/16, 9/8).
        void RemoteFxInverseWavelet(int16_t* buffer, bool extrapolate);

        // A 64x64 tile's Y, Cb and Cr (5 fractional bits, Y offset by 128) to 0x00RRGGBB.
        void RemoteFxYCbCrToRgb(const int16_t* y, const int16_t* cb, const int16_t* cr, uint32_t* out);
    } // namespace Rdp
} // namespace Marconi
