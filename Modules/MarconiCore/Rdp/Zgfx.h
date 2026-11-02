// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP 8.0 bulk decompression (MS-RDPEGFX 2.2.5 and 3.1.9.1, often called ZGFX): every graphics pipeline message
// arrives as RDP_SEGMENTED_DATA, one or more segments that are each either stored or compressed against a 2.5 MB
// history shared by the whole connection.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // One per graphics pipeline channel: the history carries over from message to message, so messages must be
        // given to the same decompressor in the order they arrived.
        class ZgfxDecompressor
        {
        public:
            ZgfxDecompressor();

            // Unpacks one RDP_SEGMENTED_DATA (a single segment, descriptor 0xE0, or several, 0xE1) into `out`.
            // False with `error` if the data is malformed; the history is then unreliable and the channel should
            // be dropped.
            bool Decompress(const uint8_t* data, size_t size, Bytes& out, std::string& error);

        private:
            // One RDP8_BULK_ENCODED_DATA: a header byte, then the stored or compressed bytes. Appends to `out`.
            bool DecompressSegment(const uint8_t* data, size_t size, Bytes& out, std::string& error);
            void PutHistory(uint8_t byte);

            std::vector<uint8_t> m_history;
            size_t m_historyIndex = 0;
        };
    } // namespace Rdp
} // namespace Marconi
