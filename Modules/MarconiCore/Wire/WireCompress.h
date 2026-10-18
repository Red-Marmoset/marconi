// Copyright (c) 2026 Red Marmoset Studios Ltd.
// zlib compression for the protocol's still pictures, in one place so the server and the client agree on it

#pragma once

#include <MarconiCore/Wire/WireStream.h>

#include <cstddef>

namespace Marconi
{
    namespace Wire
    {
        // Deflates `data` with zlib. Used for Still rectangles: a screen is mostly flat colour and repeated text,
        // which zlib shrinks a lot, so far less travels than raw pixels.
        Bytes ZlibCompress(const uint8_t* data, size_t length, int level = 6);
        inline Bytes ZlibCompress(const Bytes& data, int level = 6)
        {
            return ZlibCompress(data.data(), data.size(), level);
        }

        // Inflates `data`, which must expand to exactly `expectedSize` bytes. False if it doesn't (a corrupt or
        // hostile message), so the caller drops the connection instead of trusting a wrong-sized result.
        bool ZlibDecompress(const Bytes& data, Bytes& out, size_t expectedSize);
    } // namespace Wire
} // namespace Marconi
