// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A persistent zlib inflate stream: ZRLE and Tight keep one stream alive across many rectangles

#pragma once

#include <MarconiCore/Rfb/Stream.h>

#include <memory>

namespace Marconi
{
    namespace Rfb
    {
        class ZlibStream
        {
        public:
            // A single rectangle can't legitimately decompress to more than this (a 16k x 16k desktop of raw pixels).
            static constexpr size_t kDefaultMaxOutput = 1024ull * 1024 * 1024;

            // `maxOutput` caps what one Inflate may produce; tests lower it to check the cap without a 1 GiB buffer.
            explicit ZlibStream(size_t maxOutput = kDefaultMaxOutput);
            ~ZlibStream();

            ZlibStream(const ZlibStream&) = delete;
            ZlibStream& operator=(const ZlibStream&) = delete;

            // Discards the stream state; the next Inflate starts a fresh zlib stream.
            void Reset();

            // Decompresses all of `input` (a sync-flushed chunk of the stream) and returns the output.
            // `expectedSize` is a hint for the output size (0 if unknown). Throws RfbError on corrupt data.
            Bytes Inflate(const Bytes& input, size_t expectedSize = 0);

        private:
            struct State;
            std::unique_ptr<State> m_state;
            size_t m_maxOutput;
        };
    } // namespace Rfb
} // namespace Marconi
