// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Little-endian reader and writer for the Marconi protocol

#include <MarconiCore/Wire/WireStream.h>

#include <cstring>

namespace Marconi
{
    namespace Wire
    {
        void Reader::Read(void* out, size_t length)
        {
            if (length > m_size - m_position)
            {
                throw WireError("message ended early");
            }
            std::memcpy(out, m_data + m_position, length);
            m_position += length;
        }

        void Reader::Skip(size_t length)
        {
            if (length > m_size - m_position)
            {
                throw WireError("message ended early");
            }
            m_position += length;
        }

        uint8_t Reader::U8()
        {
            if (m_position >= m_size)
            {
                throw WireError("message ended early");
            }
            return m_data[m_position++];
        }

        uint16_t Reader::U16()
        {
            uint8_t p[2];
            Read(p, 2);
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        uint32_t Reader::U32()
        {
            uint8_t p[4];
            Read(p, 4);
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        uint64_t Reader::U64()
        {
            uint8_t p[8];
            Read(p, 8);
            uint64_t value = 0;
            for (int i = 7; i >= 0; --i)
            {
                value = (value << 8) | p[i];
            }
            return value;
        }

        uint64_t Reader::Varint()
        {
            uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                uint8_t byte = U8();
                // The final byte of a 64-bit value carries only one meaningful bit; reject anything set above it.
                if (shift == 63 && (byte & 0xFE) != 0)
                {
                    throw WireError("varint too large");
                }
                value |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0)
                {
                    return value;
                }
            }
            throw WireError("varint too long");
        }

        Bytes Reader::Blob()
        {
            uint64_t length = Varint();
            if (length > m_size - m_position)
            {
                throw WireError("blob runs past the end of the message");
            }
            Bytes bytes(static_cast<size_t>(length));
            Read(bytes.data(), bytes.size());
            return bytes;
        }

        std::string Reader::String()
        {
            Bytes bytes = Blob();
            return std::string(bytes.begin(), bytes.end());
        }

        Writer& Writer::U8(uint8_t value)
        {
            m_data.push_back(value);
            return *this;
        }

        Writer& Writer::U16(uint16_t value)
        {
            m_data.push_back(static_cast<uint8_t>(value));
            m_data.push_back(static_cast<uint8_t>(value >> 8));
            return *this;
        }

        Writer& Writer::U32(uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                m_data.push_back(static_cast<uint8_t>(value >> (i * 8)));
            }
            return *this;
        }

        Writer& Writer::U64(uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                m_data.push_back(static_cast<uint8_t>(value >> (i * 8)));
            }
            return *this;
        }

        Writer& Writer::Varint(uint64_t value)
        {
            for (;;)
            {
                uint8_t byte = value & 0x7F;
                value >>= 7;
                if (value != 0)
                {
                    m_data.push_back(byte | 0x80);
                }
                else
                {
                    m_data.push_back(byte);
                    return *this;
                }
            }
        }

        Writer& Writer::Blob(const void* data, size_t length)
        {
            Varint(length);
            return Raw(data, length);
        }

        Writer& Writer::Raw(const void* data, size_t length)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            m_data.insert(m_data.end(), bytes, bytes + length);
            return *this;
        }

        size_t VarintSize(uint64_t value)
        {
            size_t count = 1;
            while (value >= 0x80)
            {
                value >>= 7;
                ++count;
            }
            return count;
        }
    } // namespace Wire
} // namespace Marconi
