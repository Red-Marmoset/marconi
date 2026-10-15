// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A persistent zlib inflate stream: ZRLE and Tight keep one stream alive across many rectangles

#include <RmVncCore/Rfb/Decoders/ZlibStream.h>

#include <zlib.h> // wx's bundled zlib (symbols prefixed wx_zlib_)

#include <algorithm>

namespace RmVnc
{
    namespace Rfb
    {
        namespace
        {
            // A single rectangle can't legitimately decompress to more than this (a 16k x 16k desktop of raw pixels).
            constexpr size_t kMaxOutput = 1024ull * 1024 * 1024;
        }

        struct ZlibStream::State
        {
            z_stream stream = {};
            bool initialised = false;
        };

        ZlibStream::ZlibStream() : m_state(std::make_unique<State>())
        {
        }

        ZlibStream::~ZlibStream()
        {
            Reset();
        }

        void ZlibStream::Reset()
        {
            if (m_state->initialised)
            {
                inflateEnd(&m_state->stream);
                m_state->stream = {};
                m_state->initialised = false;
            }
        }

        Bytes ZlibStream::Inflate(const Bytes& input, size_t expectedSize)
        {
            z_stream& stream = m_state->stream;
            if (!m_state->initialised)
            {
                stream = {};
                if (inflateInit(&stream) != Z_OK)
                {
                    throw RfbError("Could not initialise zlib");
                }
                m_state->initialised = true;
            }

            Bytes output(std::max<size_t>(expectedSize, input.size() * 4 + 1024));
            size_t produced = 0;
            stream.next_in = const_cast<Bytef*>(input.data());
            stream.avail_in = static_cast<uInt>(input.size());

            for (;;)
            {
                if (produced == output.size())
                {
                    if (output.size() >= kMaxOutput)
                    {
                        throw RfbError("Compressed data expands too far");
                    }
                    output.resize(std::min(output.size() * 2, kMaxOutput));
                }
                stream.next_out = output.data() + produced;
                stream.avail_out = static_cast<uInt>(output.size() - produced);
                int status = inflate(&stream, Z_SYNC_FLUSH);
                produced = output.size() - stream.avail_out;

                if (status == Z_STREAM_END)
                {
                    // Some servers end the stream; the next chunk starts a new one.
                    Reset();
                    break;
                }
                if (status != Z_OK && status != Z_BUF_ERROR)
                {
                    throw RfbError(std::string("Corrupt compressed data: ") + (stream.msg ? stream.msg : "zlib error"));
                }
                if (stream.avail_out > 0)
                {
                    break; // inflate stopped with room to spare: all input it can use is consumed
                }
                // Output buffer full: grow it and continue.
            }
            if (stream.avail_in != 0 && m_state->initialised)
            {
                throw RfbError("Compressed data was not fully consumed");
            }
            output.resize(produced);
            return output;
        }
    } // namespace Rfb
} // namespace RmVnc
