// Copyright (c) 2026 Red Marmoset Studios Ltd.
// zlib compression for still pictures (wx's bundled zlib, as the decoders use)

#include <MarconiCore/Wire/WireCompress.h>

#include <zlib.h>

namespace Marconi
{
    namespace Wire
    {
        Bytes ZlibCompress(const uint8_t* data, size_t length, int level)
        {
            uLongf bound = compressBound(static_cast<uLong>(length));
            Bytes out(bound);
            uLongf produced = bound;
            if (compress2(out.data(), &produced, data, static_cast<uLong>(length), level) != Z_OK)
            {
                return {}; // the caller treats an empty result as "couldn't compress" and sends raw instead
            }
            out.resize(produced);
            return out;
        }

        bool ZlibDecompress(const Bytes& data, Bytes& out, size_t expectedSize)
        {
            out.resize(expectedSize);
            uLongf produced = static_cast<uLongf>(expectedSize);
            int result = uncompress(out.data(), &produced, data.data(), static_cast<uLong>(data.size()));
            if (result != Z_OK || produced != expectedSize)
            {
                return false;
            }
            return true;
        }
    } // namespace Wire
} // namespace Marconi
