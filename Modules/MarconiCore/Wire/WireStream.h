// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Little-endian reader and writer for the Marconi protocol: fixed-width integers, varints and length-prefixed blobs

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        using Bytes = std::vector<uint8_t>;

        // The largest a single framed message may be. A reader refuses a length prefix beyond this instead of
        // trying to allocate it, so a corrupt or hostile length can't exhaust memory. A picture larger than this
        // is split across several messages.
        constexpr uint64_t kMaxMessageBytes = 64 * 1024 * 1024;

        // Thrown when a message can't be parsed (a length past the end, a varint that never terminates, a string
        // that isn't valid): the connection is dropped. The message is for the log, not the user.
        class WireError : public std::runtime_error
        {
        public:
            using std::runtime_error::runtime_error;
        };

        // Reads from a block of bytes already in memory (one whole message; framing has separated them). Every read
        // that would run past the end throws WireError, so a parser can read straight through without checking each
        // call. Numbers are little-endian, the native order on every platform Marconi runs on, so nothing is
        // byte-swapped.
        class Reader
        {
        public:
            Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
            explicit Reader(const Bytes& bytes) : Reader(bytes.data(), bytes.size()) {}

            uint8_t U8();
            uint16_t U16();
            uint32_t U32();
            uint64_t U64();
            int32_t S32() { return static_cast<int32_t>(U32()); }
            // LEB128 unsigned varint: 7 bits per byte, low byte first, top bit set while more follow. At most ten
            // bytes (a 64-bit value); a longer run, or one that overflows 64 bits, throws.
            uint64_t Varint();
            // A varint length followed by that many bytes.
            Bytes Blob();
            std::string String();
            void Read(void* out, size_t length);
            void Skip(size_t length);

            size_t Remaining() const { return m_size - m_position; }
            bool AtEnd() const { return m_position == m_size; }

        private:
            const uint8_t* m_data;
            size_t m_size;
            size_t m_position = 0;
        };

        // Appends to a growable buffer. Every call returns *this so writes can be chained.
        class Writer
        {
        public:
            Writer& U8(uint8_t value);
            Writer& U16(uint16_t value);
            Writer& U32(uint32_t value);
            Writer& U64(uint64_t value);
            Writer& S32(int32_t value) { return U32(static_cast<uint32_t>(value)); }
            Writer& Varint(uint64_t value);
            // A varint length followed by the bytes.
            Writer& Blob(const void* data, size_t length);
            Writer& Blob(const Bytes& bytes) { return Blob(bytes.data(), bytes.size()); }
            Writer& String(const std::string& text) { return Blob(text.data(), text.size()); }
            Writer& Raw(const void* data, size_t length);
            Writer& Raw(const Bytes& bytes) { return Raw(bytes.data(), bytes.size()); }

            const Bytes& Data() const { return m_data; }
            Bytes Take() { return std::move(m_data); }
            size_t Size() const { return m_data.size(); }

        private:
            Bytes m_data;
        };

        // The number of bytes Varint(value) writes (1 to 10), for sizing a buffer up front.
        size_t VarintSize(uint64_t value);
    } // namespace Wire
} // namespace Marconi
