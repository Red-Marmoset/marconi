// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP 8.0 bulk decompression for the graphics pipeline (MS-RDPEGFX 3.1.9.1).

#include <MarconiCore/Rdp/Zgfx.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr size_t kHistorySize = 2500000;
            constexpr size_t kMaxSegmentOutput = 65535;
            constexpr uint8_t kDescriptorSingle = 0xE0;
            constexpr uint8_t kDescriptorMultipart = 0xE1;
            constexpr uint8_t kCompressionTypeMask = 0x0F;
            constexpr uint8_t kCompressionRdp8 = 0x04;
            constexpr uint8_t kPacketCompressed = 0x20;

            // The token table of MS-RDPEGFX 3.1.9.1.2: a prefix code, then either a literal (`isMatch` false: the
            // byte is `base` plus `valueBits` bits) or a match distance (`base` plus `valueBits` bits; distance 0
            // means a run of unencoded bytes follows).
            struct Token
            {
                uint8_t prefixLength;
                uint16_t prefixCode;
                uint8_t valueBits;
                bool isMatch;
                uint32_t base;
            };

            const Token kTokens[] = {
                {1, 0x000, 8, false, 0},          {5, 0x011, 5, true, 0},           {5, 0x012, 7, true, 32},
                {5, 0x013, 9, true, 160},         {5, 0x014, 10, true, 672},        {5, 0x015, 12, true, 1696},
                {5, 0x018, 0, false, 0x00},       {5, 0x019, 0, false, 0x01},       {6, 0x02C, 14, true, 5792},
                {6, 0x02D, 15, true, 22176},      {6, 0x034, 0, false, 0x02},       {6, 0x035, 0, false, 0x03},
                {6, 0x036, 0, false, 0xFF},       {7, 0x05C, 18, true, 54944},      {7, 0x05D, 20, true, 317088},
                {7, 0x06E, 0, false, 0x04},       {7, 0x06F, 0, false, 0x05},       {7, 0x070, 0, false, 0x06},
                {7, 0x071, 0, false, 0x07},       {7, 0x072, 0, false, 0x08},       {7, 0x073, 0, false, 0x09},
                {7, 0x074, 0, false, 0x0A},       {7, 0x075, 0, false, 0x0B},       {7, 0x076, 0, false, 0x3A},
                {7, 0x077, 0, false, 0x3B},       {7, 0x078, 0, false, 0x3C},       {7, 0x079, 0, false, 0x3D},
                {7, 0x07A, 0, false, 0x3E},       {7, 0x07B, 0, false, 0x3F},       {7, 0x07C, 0, false, 0x40},
                {7, 0x07D, 0, false, 0x80},       {8, 0x0BC, 20, true, 1365664},    {8, 0x0BD, 21, true, 2414240},
                {8, 0x0FC, 0, false, 0x0C},       {8, 0x0FD, 0, false, 0x38},       {8, 0x0FE, 0, false, 0x39},
                {8, 0x0FF, 0, false, 0x66},       {9, 0x17C, 22, true, 4511392},    {9, 0x17D, 23, true, 8705696},
                {9, 0x17E, 24, true, 17094304},
            };

            // Reads bits most significant first from a compressed segment, stopping at the bit count the segment
            // declares (its last byte says how many padding bits the byte before it holds).
            class BitReader
            {
            public:
                BitReader(const uint8_t* data, size_t bytes, size_t bits) : m_data(data), m_bytes(bytes), m_left(bits)
                {
                }

                bool Bits(int count, uint32_t& value)
                {
                    value = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        if (m_left == 0 || m_pos >= m_bytes)
                        {
                            return false;
                        }
                        uint32_t bit = (m_data[m_pos] >> (7 - m_bit)) & 1;
                        value = (value << 1) | bit;
                        --m_left;
                        if (++m_bit == 8)
                        {
                            m_bit = 0;
                            ++m_pos;
                        }
                    }
                    return true;
                }

                // Skips to the next byte boundary, for a run of unencoded bytes.
                void Align()
                {
                    if (m_bit != 0)
                    {
                        size_t skip = 8 - m_bit;
                        m_left = m_left > skip ? m_left - skip : 0;
                        m_bit = 0;
                        ++m_pos;
                    }
                }

                // Takes `count` whole bytes (after Align).
                bool TakeBytes(size_t count, const uint8_t*& start)
                {
                    if (m_bit != 0 || count * 8 > m_left || m_pos + count > m_bytes)
                    {
                        return false;
                    }
                    start = m_data + m_pos;
                    m_pos += count;
                    m_left -= count * 8;
                    return true;
                }

                bool Done() const { return m_left == 0; }

            private:
                const uint8_t* m_data;
                size_t m_bytes;
                size_t m_left;
                size_t m_pos = 0;
                int m_bit = 0;
            };

            uint32_t GetU32(const uint8_t* data)
            {
                return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                       (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
            }
        } // namespace

        ZgfxDecompressor::ZgfxDecompressor() : m_history(kHistorySize, 0)
        {
        }

        void ZgfxDecompressor::PutHistory(uint8_t byte)
        {
            m_history[m_historyIndex] = byte;
            if (++m_historyIndex == kHistorySize)
            {
                m_historyIndex = 0;
            }
        }

        bool ZgfxDecompressor::DecompressSegment(const uint8_t* data, size_t size, Bytes& out, std::string& error)
        {
            if (size < 1)
            {
                error = "an empty graphics segment";
                return false;
            }
            uint8_t header = data[0];
            if ((header & kCompressionTypeMask) != kCompressionRdp8)
            {
                error = "a graphics segment with an unknown compression type";
                return false;
            }
            const uint8_t* body = data + 1;
            size_t bodySize = size - 1;
            if (!(header & kPacketCompressed))
            {
                // Stored as is, but still part of the history later matches refer to.
                for (size_t i = 0; i < bodySize; ++i)
                {
                    PutHistory(body[i]);
                }
                out.insert(out.end(), body, body + bodySize);
                return true;
            }
            if (bodySize < 1)
            {
                error = "a compressed graphics segment with no data";
                return false;
            }
            size_t padding = body[bodySize - 1];
            size_t totalBits = (bodySize - 1) * 8;
            if (padding > totalBits)
            {
                error = "a compressed graphics segment with a bad length";
                return false;
            }
            BitReader reader(body, bodySize - 1, totalBits - padding);
            size_t produced = 0;
            while (!reader.Done())
            {
                // Find the token whose prefix matches, reading one bit at a time (the prefixes are a prefix code
                // sorted by length).
                uint32_t prefix = 0;
                int haveBits = 0;
                const Token* token = nullptr;
                for (const Token& candidate : kTokens)
                {
                    while (haveBits < candidate.prefixLength)
                    {
                        uint32_t bit = 0;
                        if (!reader.Bits(1, bit))
                        {
                            error = "a compressed graphics segment ended in the middle of a token";
                            return false;
                        }
                        prefix = (prefix << 1) | bit;
                        ++haveBits;
                    }
                    if (prefix == candidate.prefixCode)
                    {
                        token = &candidate;
                        break;
                    }
                }
                uint32_t value = 0;
                if (token == nullptr || !reader.Bits(token->valueBits, value))
                {
                    error = "a compressed graphics segment has a token that doesn't decode";
                    return false;
                }
                if (!token->isMatch)
                {
                    uint8_t literal = static_cast<uint8_t>(token->base + value);
                    if (++produced > kMaxSegmentOutput)
                    {
                        error = "a graphics segment decompresses to more than a segment may hold";
                        return false;
                    }
                    PutHistory(literal);
                    out.push_back(literal);
                    continue;
                }
                uint32_t distance = token->base + value;
                if (distance == 0)
                {
                    // A run of bytes stored unencoded, starting at the next byte boundary.
                    uint32_t count = 0;
                    const uint8_t* start = nullptr;
                    if (!reader.Bits(15, count))
                    {
                        error = "a compressed graphics segment ended in the middle of a token";
                        return false;
                    }
                    reader.Align();
                    if (!reader.TakeBytes(count, start) || (produced += count) > kMaxSegmentOutput)
                    {
                        error = "a compressed graphics segment has an unencoded run that doesn't fit";
                        return false;
                    }
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        PutHistory(start[i]);
                    }
                    out.insert(out.end(), start, start + count);
                    continue;
                }
                // A copy of earlier output: 3 bytes, or 4, 8, 16... plus extra bits, from `distance` back.
                uint32_t bit = 0;
                uint32_t count = 3;
                if (!reader.Bits(1, bit))
                {
                    error = "a compressed graphics segment ended in the middle of a token";
                    return false;
                }
                if (bit != 0)
                {
                    count = 4;
                    int extra = 2;
                    if (!reader.Bits(1, bit))
                    {
                        error = "a compressed graphics segment ended in the middle of a token";
                        return false;
                    }
                    while (bit == 1)
                    {
                        count *= 2;
                        ++extra;
                        if (extra > 16 || !reader.Bits(1, bit))
                        {
                            error = "a compressed graphics segment has a match that doesn't decode";
                            return false;
                        }
                    }
                    uint32_t more = 0;
                    if (!reader.Bits(extra, more))
                    {
                        error = "a compressed graphics segment ended in the middle of a token";
                        return false;
                    }
                    count += more;
                }
                if (distance > kHistorySize || (produced += count) > kMaxSegmentOutput)
                {
                    error = "a compressed graphics segment has a match that doesn't fit";
                    return false;
                }
                // Byte by byte, so a match longer than its distance repeats the pattern, as the format intends.
                for (uint32_t i = 0; i < count; ++i)
                {
                    uint8_t byte = m_history[(m_historyIndex + kHistorySize - distance) % kHistorySize];
                    PutHistory(byte);
                    out.push_back(byte);
                }
            }
            return true;
        }

        bool ZgfxDecompressor::Decompress(const uint8_t* data, size_t size, Bytes& out, std::string& error)
        {
            out.clear();
            if (size < 1)
            {
                error = "an empty graphics message";
                return false;
            }
            if (data[0] == kDescriptorSingle)
            {
                return DecompressSegment(data + 1, size - 1, out, error);
            }
            if (data[0] != kDescriptorMultipart || size < 7)
            {
                error = "a graphics message that isn't segmented data";
                return false;
            }
            uint16_t segmentCount = static_cast<uint16_t>(data[1] | (data[2] << 8));
            uint32_t uncompressedSize = GetU32(data + 3);
            if (uncompressedSize > 64 * 1024 * 1024)
            {
                error = "a graphics message too large to be real";
                return false;
            }
            out.reserve(uncompressedSize);
            size_t p = 7;
            for (uint16_t i = 0; i < segmentCount; ++i)
            {
                if (p + 4 > size)
                {
                    error = "a graphics message ends before its segments do";
                    return false;
                }
                uint32_t segmentSize = GetU32(data + p);
                p += 4;
                if (segmentSize > size - p)
                {
                    error = "a graphics message ends before its segments do";
                    return false;
                }
                if (!DecompressSegment(data + p, segmentSize, out, error))
                {
                    return false;
                }
                p += segmentSize;
            }
            if (out.size() != uncompressedSize)
            {
                error = "a graphics message didn't decompress to the size it declared";
                return false;
            }
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
