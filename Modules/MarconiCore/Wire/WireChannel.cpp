// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Framed messages over a byte transport

#include <MarconiCore/Wire/WireChannel.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            constexpr size_t kReadChunk = 64 * 1024;
        }

        uint8_t MessageReader::ReadByte()
        {
            if (m_position == m_buffer.size())
            {
                m_buffer.resize(kReadChunk);
                ptrdiff_t got = m_transport.Read(m_buffer.data(), m_buffer.size());
                if (got <= 0)
                {
                    throw WireError(got == 0 ? "the other end closed the connection" : "the connection failed");
                }
                m_buffer.resize(static_cast<size_t>(got));
                m_position = 0;
            }
            return m_buffer[m_position++];
        }

        void MessageReader::ReadExactly(uint8_t* out, size_t length)
        {
            while (length > 0)
            {
                if (m_position == m_buffer.size())
                {
                    m_buffer.resize(kReadChunk);
                    ptrdiff_t got = m_transport.Read(m_buffer.data(), m_buffer.size());
                    if (got <= 0)
                    {
                        throw WireError(got == 0 ? "the other end closed the connection" : "the connection failed");
                    }
                    m_buffer.resize(static_cast<size_t>(got));
                    m_position = 0;
                }
                size_t available = m_buffer.size() - m_position;
                size_t take = std::min(available, length);
                std::memcpy(out, m_buffer.data() + m_position, take);
                m_position += take;
                out += take;
                length -= take;
            }
        }

        Bytes MessageReader::Next()
        {
            // The varint length, read one byte at a time straight off the stream (it is at most ten bytes).
            uint64_t length = 0;
            for (int shift = 0;; shift += 7)
            {
                if (shift >= 64)
                {
                    throw WireError("message length varint too long");
                }
                uint8_t byte = ReadByte();
                if (shift == 63 && (byte & 0xFE) != 0)
                {
                    throw WireError("message length varint too large");
                }
                length |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0)
                {
                    break;
                }
            }
            if (length > kMaxMessageBytes)
            {
                throw WireError("message larger than the protocol allows");
            }
            Bytes body(static_cast<size_t>(length));
            ReadExactly(body.data(), body.size());
            return body;
        }

        bool WriteMessage(Transport& transport, const Bytes& framed)
        {
            return transport.Write(framed.data(), framed.size());
        }
    } // namespace Wire
} // namespace Marconi
