// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Buffered big-endian reader and message builder for the RFB protocol

#include <MarconiCore/Rfb/Stream.h>

#include <MarconiCore/Net/Socket.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            constexpr size_t kBufferSize = 64 * 1024;
        }

        ptrdiff_t SocketSource::Read(void* buffer, size_t length)
        {
            return m_socket.Receive(buffer, length);
        }

        ptrdiff_t MemorySource::Read(void* buffer, size_t length)
        {
            size_t count = std::min(length, m_data.size() - m_position);
            std::memcpy(buffer, m_data.data() + m_position, count);
            m_position += count;
            return static_cast<ptrdiff_t>(count);
        }

        InStream::InStream(ByteSource& source) : m_source(source), m_buffer(kBufferSize)
        {
        }

        void InStream::Fill(size_t minimum)
        {
            if (m_end - m_start >= minimum)
            {
                return;
            }
            // Compact so there is room for `minimum` bytes after m_start.
            if (m_start > 0)
            {
                std::memmove(m_buffer.data(), m_buffer.data() + m_start, m_end - m_start);
                m_end -= m_start;
                m_start = 0;
            }
            if (m_buffer.size() < minimum)
            {
                m_buffer.resize(minimum);
            }
            while (m_end < minimum)
            {
                ptrdiff_t got = m_source.Read(m_buffer.data() + m_end, m_buffer.size() - m_end);
                if (got == 0)
                {
                    throw RfbError("The server closed the connection");
                }
                if (got < 0)
                {
                    throw RfbError("Connection lost");
                }
                m_end += static_cast<size_t>(got);
            }
        }

        uint8_t InStream::U8()
        {
            Fill(1);
            ++m_totalRead;
            return m_buffer[m_start++];
        }

        uint16_t InStream::U16()
        {
            Fill(2);
            uint16_t value = static_cast<uint16_t>((m_buffer[m_start] << 8) | m_buffer[m_start + 1]);
            m_start += 2;
            m_totalRead += 2;
            return value;
        }

        uint32_t InStream::U32()
        {
            Fill(4);
            const uint8_t* p = m_buffer.data() + m_start;
            uint32_t value = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                             (static_cast<uint32_t>(p[2]) << 8) | p[3];
            m_start += 4;
            m_totalRead += 4;
            return value;
        }

        void InStream::Read(void* out, size_t length)
        {
            uint8_t* destination = static_cast<uint8_t*>(out);
            while (length > 0)
            {
                Fill(1);
                size_t count = std::min(length, m_end - m_start);
                std::memcpy(destination, m_buffer.data() + m_start, count);
                m_start += count;
                m_totalRead += count;
                destination += count;
                length -= count;
            }
        }

        Bytes InStream::ReadBytes(size_t length)
        {
            Bytes bytes(length);
            Read(bytes.data(), length);
            return bytes;
        }

        std::string InStream::ReadString(size_t length)
        {
            std::string text(length, '\0');
            Read(text.data(), length);
            return text;
        }

        void InStream::Skip(size_t length)
        {
            while (length > 0)
            {
                Fill(1);
                size_t count = std::min(length, m_end - m_start);
                m_start += count;
                m_totalRead += count;
                length -= count;
            }
        }

        OutBuffer& OutBuffer::U8(uint8_t value)
        {
            m_data.push_back(value);
            return *this;
        }

        OutBuffer& OutBuffer::U16(uint16_t value)
        {
            m_data.push_back(static_cast<uint8_t>(value >> 8));
            m_data.push_back(static_cast<uint8_t>(value));
            return *this;
        }

        OutBuffer& OutBuffer::U32(uint32_t value)
        {
            m_data.push_back(static_cast<uint8_t>(value >> 24));
            m_data.push_back(static_cast<uint8_t>(value >> 16));
            m_data.push_back(static_cast<uint8_t>(value >> 8));
            m_data.push_back(static_cast<uint8_t>(value));
            return *this;
        }

        OutBuffer& OutBuffer::Padding(size_t count)
        {
            m_data.insert(m_data.end(), count, 0);
            return *this;
        }

        OutBuffer& OutBuffer::Append(const void* data, size_t length)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            m_data.insert(m_data.end(), bytes, bytes + length);
            return *this;
        }
    } // namespace Rfb
} // namespace Marconi
