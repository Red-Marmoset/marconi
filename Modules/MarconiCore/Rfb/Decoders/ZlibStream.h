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
            ZlibStream();
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
        };
    } // namespace Rfb
} // namespace Marconi
