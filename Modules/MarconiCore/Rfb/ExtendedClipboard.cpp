// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Extended Clipboard pseudo-encoding: clipboard messages that carry UTF-8 text and other formats, compressed

#include <MarconiCore/Rfb/ExtendedClipboard.h>

#include <MarconiCore/Rfb/Messages.h>

#include <zlib.h> // wx's bundled zlib (symbols prefixed wx_zlib_)

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rfb
    {
        namespace ExtendedClipboard
        {
            namespace
            {
                uint32_t ReadU32(const Bytes& bytes, size_t offset)
                {
                    return static_cast<uint32_t>(bytes[offset]) << 24 | static_cast<uint32_t>(bytes[offset + 1]) << 16 |
                           static_cast<uint32_t>(bytes[offset + 2]) << 8 | bytes[offset + 3];
                }

                Bytes Deflate(const Bytes& input)
                {
                    z_stream stream = {};
                    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
                    {
                        throw RfbError("Could not compress the clipboard");
                    }
                    Bytes output(deflateBound(&stream, static_cast<uLong>(input.size())));
                    stream.next_in = const_cast<Bytef*>(input.data());
                    stream.avail_in = static_cast<uInt>(input.size());
                    stream.next_out = output.data();
                    stream.avail_out = static_cast<uInt>(output.size());
                    int result = deflate(&stream, Z_FINISH);
                    output.resize(stream.total_out);
                    deflateEnd(&stream);
                    if (result != Z_STREAM_END)
                    {
                        throw RfbError("Could not compress the clipboard");
                    }
                    return output;
                }

                Bytes Inflate(const Bytes& input, size_t maxOutput)
                {
                    z_stream stream = {};
                    if (inflateInit(&stream) != Z_OK)
                    {
                        throw RfbError("Could not decompress the clipboard");
                    }
                    Bytes output;
                    stream.next_in = const_cast<Bytef*>(input.data());
                    stream.avail_in = static_cast<uInt>(input.size());
                    uint8_t chunk[64 * 1024];
                    int result = Z_OK;
                    while (result != Z_STREAM_END)
                    {
                        stream.next_out = chunk;
                        stream.avail_out = sizeof(chunk);
                        result = inflate(&stream, Z_NO_FLUSH);
                        if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
                        {
                            inflateEnd(&stream);
                            throw RfbError("The server sent a corrupt clipboard message");
                        }
                        size_t produced = sizeof(chunk) - stream.avail_out;
                        if (output.size() + produced > maxOutput)
                        {
                            inflateEnd(&stream);
                            throw RfbError("The server sent an oversized clipboard update");
                        }
                        output.insert(output.end(), chunk, chunk + produced);
                        if (result == Z_BUF_ERROR || (produced == 0 && stream.avail_in == 0))
                        {
                            break; // the input ran out before the end of the stream: take what there is
                        }
                    }
                    inflateEnd(&stream);
                    return output;
                }

                Bytes Flags(uint32_t flags)
                {
                    OutBuffer out;
                    out.U32(flags);
                    return out.Data();
                }
            } // namespace

            Message Parse(const Bytes& body, size_t maxProvideBytes)
            {
                if (body.size() < 4)
                {
                    throw RfbError("The server sent a truncated clipboard message");
                }
                Message message;
                message.flags = ReadU32(body, 0);
                uint32_t formats = message.GetFormats();
                if (message.Has(Action::Caps))
                {
                    size_t offset = 4;
                    for (uint32_t bit = 1; bit <= Format::Mask; bit <<= 1)
                    {
                        if ((formats & bit) == 0)
                        {
                            continue;
                        }
                        if (offset + 4 > body.size())
                        {
                            throw RfbError("The server sent a truncated clipboard message");
                        }
                        message.maxSizes[bit] = ReadU32(body, offset);
                        offset += 4;
                    }
                }
                else if (message.Has(Action::Provide))
                {
                    Bytes compressed(body.begin() + 4, body.end());
                    Bytes plain = Inflate(compressed, maxProvideBytes);
                    size_t offset = 0;
                    for (uint32_t bit = 1; bit <= Format::Mask; bit <<= 1)
                    {
                        if ((formats & bit) == 0)
                        {
                            continue;
                        }
                        if (offset + 4 > plain.size())
                        {
                            throw RfbError("The server sent a truncated clipboard message");
                        }
                        uint32_t size = ReadU32(plain, offset);
                        offset += 4;
                        if (size > plain.size() - offset)
                        {
                            throw RfbError("The server sent a truncated clipboard message");
                        }
                        message.data[bit] = Bytes(plain.begin() + static_cast<ptrdiff_t>(offset),
                                                  plain.begin() + static_cast<ptrdiff_t>(offset + size));
                        offset += size;
                    }
                }
                return message;
            }

            Bytes BuildCaps(const std::map<uint32_t, uint32_t>& maxSizes, uint32_t actions)
            {
                uint32_t formats = 0;
                for (const auto& entry : maxSizes)
                {
                    formats |= entry.first;
                }
                OutBuffer out;
                out.U32(Action::Caps | (actions & Action::Mask) | (formats & Format::Mask));
                for (uint32_t bit = 1; bit <= Format::Mask; bit <<= 1)
                {
                    auto found = maxSizes.find(bit);
                    if (found != maxSizes.end())
                    {
                        out.U32(found->second);
                    }
                }
                return out.Data();
            }

            Bytes BuildNotify(uint32_t formats)
            {
                return Flags(Action::Notify | (formats & Format::Mask));
            }

            Bytes BuildRequest(uint32_t formats)
            {
                return Flags(Action::Request | (formats & Format::Mask));
            }

            Bytes BuildPeek()
            {
                return Flags(Action::Peek);
            }

            Bytes BuildProvide(const std::map<uint32_t, Bytes>& data)
            {
                uint32_t formats = 0;
                OutBuffer plain;
                for (uint32_t bit = 1; bit <= Format::Mask; bit <<= 1)
                {
                    auto found = data.find(bit);
                    if (found != data.end())
                    {
                        formats |= bit;
                        plain.U32(static_cast<uint32_t>(found->second.size()));
                        plain.Append(found->second.data(), found->second.size());
                    }
                }
                Bytes compressed = Deflate(plain.Data());
                OutBuffer out;
                out.U32(Action::Provide | formats);
                out.Append(compressed.data(), compressed.size());
                return out.Data();
            }

            Bytes WrapClientMessage(const Bytes& body)
            {
                OutBuffer out;
                out.U8(ClientMessage::ClientCutText).Padding(3).S32(-static_cast<int32_t>(body.size()));
                out.Append(body.data(), body.size());
                return out.Data();
            }

            Bytes TextToWire(const std::string& utf8)
            {
                Bytes wire;
                wire.reserve(utf8.size() + 1);
                for (size_t i = 0; i < utf8.size(); ++i)
                {
                    char c = utf8[i];
                    if (c == '\r')
                    {
                        continue; // written again before the '\n' that follows, or dropped if stray
                    }
                    if (c == '\n')
                    {
                        wire.push_back('\r');
                    }
                    wire.push_back(static_cast<uint8_t>(c));
                }
                wire.push_back(0);
                return wire;
            }

            std::string TextFromWire(const Bytes& data)
            {
                std::string text;
                text.reserve(data.size());
                for (size_t i = 0; i < data.size(); ++i)
                {
                    uint8_t byte = data[i];
                    if (byte == 0)
                    {
                        break;
                    }
                    if (byte == '\r' && i + 1 < data.size() && data[i + 1] == '\n')
                    {
                        continue;
                    }
                    text.push_back(static_cast<char>(byte));
                }
                return text;
            }
        } // namespace ExtendedClipboard
    } // namespace Rfb
} // namespace Marconi
