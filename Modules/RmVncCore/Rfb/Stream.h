// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Buffered big-endian reader and message builder for the RFB protocol

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace RmVnc
{
    namespace Net
    {
        class Socket;
    }

    namespace Rfb
    {
        using Bytes = std::vector<uint8_t>;

        // Thrown for protocol violations and lost connections; the message is shown to the user.
        class RfbError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        class ByteSource
        {
        public:
            virtual ~ByteSource() = default;
            // Blocks until at least one byte is available. Returns bytes read, 0 at end of stream, -1 on error.
            virtual ptrdiff_t Read(void* buffer, size_t length) = 0;
        };

        class SocketSource : public ByteSource
        {
        public:
            explicit SocketSource(Net::Socket& socket) : m_socket(socket) {}
            ptrdiff_t Read(void* buffer, size_t length) override;

        private:
            Net::Socket& m_socket;
        };

        class MemorySource : public ByteSource
        {
        public:
            explicit MemorySource(Bytes data) : m_data(std::move(data)) {}
            ptrdiff_t Read(void* buffer, size_t length) override;

        private:
            Bytes m_data;
            size_t m_position = 0;
        };

        class ByteSink
        {
        public:
            virtual ~ByteSink() = default;
            // Sends a complete message. Throws RfbError if the connection failed.
            virtual void Send(const Bytes& data) = 0;
        };

        class InStream
        {
        public:
            explicit InStream(ByteSource& source);

            uint8_t U8();
            uint16_t U16();
            uint32_t U32();
            int32_t S32() { return static_cast<int32_t>(U32()); }
            void Read(void* out, size_t length);
            Bytes ReadBytes(size_t length);
            std::string ReadString(size_t length);
            void Skip(size_t length);

            // Little-endian 32-bit pixel in the client pixel format we always request (see PixelFormat::Client).
            uint32_t Pixel32()
            {
                uint8_t p[4];
                Read(p, 4);
                return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
            }

            uint64_t GetBytesRead() const { return m_totalRead; }

        private:
            void Fill(size_t minimum);

            ByteSource& m_source;
            std::vector<uint8_t> m_buffer;
            size_t m_start = 0;
            size_t m_end = 0;
            uint64_t m_totalRead = 0;
        };

        class OutBuffer
        {
        public:
            OutBuffer& U8(uint8_t value);
            OutBuffer& U16(uint16_t value);
            OutBuffer& U32(uint32_t value);
            OutBuffer& S32(int32_t value) { return U32(static_cast<uint32_t>(value)); }
            OutBuffer& Padding(size_t count);
            OutBuffer& Append(const void* data, size_t length);
            OutBuffer& Append(const std::string& text) { return Append(text.data(), text.size()); }

            const Bytes& Data() const { return m_data; }
            void Clear() { m_data.clear(); }

        private:
            Bytes m_data;
        };
    } // namespace Rfb
} // namespace RmVnc
