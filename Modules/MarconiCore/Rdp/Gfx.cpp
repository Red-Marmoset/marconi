// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Parsing and building the RDP graphics pipeline's messages (MS-RDPEGFX 2.2.2).

#include <MarconiCore/Rdp/Gfx.h>

#include <cstdio>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            constexpr size_t kHeaderSize = 8;

            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>(value >> 8));
            }
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }

            // Reads little-endian fields from one PDU's body, noting when it runs out.
            class Reader
            {
            public:
                Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

                uint8_t U8()
                {
                    if (!Need(1))
                    {
                        return 0;
                    }
                    return m_data[m_pos++];
                }
                uint16_t U16()
                {
                    if (!Need(2))
                    {
                        return 0;
                    }
                    uint16_t value = static_cast<uint16_t>(m_data[m_pos] | (m_data[m_pos + 1] << 8));
                    m_pos += 2;
                    return value;
                }
                uint32_t U32()
                {
                    if (!Need(4))
                    {
                        return 0;
                    }
                    uint32_t value = static_cast<uint32_t>(m_data[m_pos]) |
                                     (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                                     (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
                                     (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
                    m_pos += 4;
                    return value;
                }
                void Skip(size_t count)
                {
                    if (Need(count))
                    {
                        m_pos += count;
                    }
                }
                GfxRect Rect()
                {
                    GfxRect rect;
                    rect.left = U16();
                    rect.top = U16();
                    rect.right = U16();
                    rect.bottom = U16();
                    return rect;
                }
                GfxPoint Point()
                {
                    GfxPoint point;
                    point.x = U16();
                    point.y = U16();
                    return point;
                }
                void Take(size_t count, Bytes& out)
                {
                    if (Need(count))
                    {
                        out.assign(m_data + m_pos, m_data + m_pos + count);
                        m_pos += count;
                    }
                }
                size_t Left() const { return m_ok ? m_size - m_pos : 0; }
                bool Ok() const { return m_ok; }

            private:
                bool Need(size_t count)
                {
                    if (!m_ok || count > m_size - m_pos)
                    {
                        m_ok = false;
                        return false;
                    }
                    return true;
                }

                const uint8_t* m_data;
                size_t m_size;
                size_t m_pos = 0;
                bool m_ok = true;
            };

            bool ParseBody(GfxCommand command, Reader& in, GfxPdu& pdu)
            {
                switch (command)
                {
                    case GfxCommand::WireToSurface1:
                    {
                        pdu.surfaceId = in.U16();
                        pdu.codecId = in.U16();
                        pdu.pixelFormat = in.U8();
                        pdu.rect = in.Rect();
                        uint32_t length = in.U32();
                        in.Take(length, pdu.data);
                        break;
                    }
                    case GfxCommand::WireToSurface2:
                    {
                        pdu.surfaceId = in.U16();
                        pdu.codecId = in.U16();
                        pdu.codecContextId = in.U32();
                        pdu.pixelFormat = in.U8();
                        uint32_t length = in.U32();
                        in.Take(length, pdu.data);
                        break;
                    }
                    case GfxCommand::DeleteEncodingContext:
                        pdu.surfaceId = in.U16();
                        pdu.codecContextId = in.U32();
                        break;
                    case GfxCommand::SolidFill:
                    {
                        pdu.surfaceId = in.U16();
                        uint8_t b = in.U8();
                        uint8_t g = in.U8();
                        uint8_t r = in.U8();
                        in.U8(); // XA
                        pdu.fillColor = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
                        uint16_t count = in.U16();
                        for (uint16_t i = 0; i < count && in.Ok(); ++i)
                        {
                            pdu.rects.push_back(in.Rect());
                        }
                        break;
                    }
                    case GfxCommand::SurfaceToSurface:
                    {
                        pdu.sourceSurfaceId = in.U16();
                        pdu.surfaceId = in.U16();
                        pdu.rect = in.Rect();
                        uint16_t count = in.U16();
                        for (uint16_t i = 0; i < count && in.Ok(); ++i)
                        {
                            pdu.points.push_back(in.Point());
                        }
                        break;
                    }
                    case GfxCommand::SurfaceToCache:
                        pdu.surfaceId = in.U16();
                        in.Skip(8); // cacheKey
                        pdu.cacheSlot = in.U16();
                        pdu.rect = in.Rect();
                        break;
                    case GfxCommand::CacheToSurface:
                    {
                        pdu.cacheSlot = in.U16();
                        pdu.surfaceId = in.U16();
                        uint16_t count = in.U16();
                        for (uint16_t i = 0; i < count && in.Ok(); ++i)
                        {
                            pdu.points.push_back(in.Point());
                        }
                        break;
                    }
                    case GfxCommand::EvictCacheEntry:
                        pdu.cacheSlot = in.U16();
                        break;
                    case GfxCommand::CreateSurface:
                        pdu.surfaceId = in.U16();
                        pdu.width = in.U16();
                        pdu.height = in.U16();
                        pdu.pixelFormat = in.U8();
                        break;
                    case GfxCommand::DeleteSurface:
                        pdu.surfaceId = in.U16();
                        break;
                    case GfxCommand::StartFrame:
                        in.U32(); // timestamp
                        pdu.frameId = in.U32();
                        break;
                    case GfxCommand::EndFrame:
                        pdu.frameId = in.U32();
                        break;
                    case GfxCommand::ResetGraphics:
                        // The monitor layout that follows is the server's; the desktop size is what matters here.
                        pdu.width = in.U32();
                        pdu.height = in.U32();
                        break;
                    case GfxCommand::MapSurfaceToOutput:
                    case GfxCommand::MapSurfaceToScaledOutput:
                        pdu.surfaceId = in.U16();
                        in.U16(); // reserved
                        pdu.outputX = in.U32();
                        pdu.outputY = in.U32();
                        if (command == GfxCommand::MapSurfaceToScaledOutput)
                        {
                            pdu.width = in.U32(); // the target size the surface is stretched to
                            pdu.height = in.U32();
                        }
                        break;
                    case GfxCommand::CapsConfirm:
                    {
                        pdu.capsVersion = in.U32();
                        uint32_t length = in.U32();
                        if (length >= 4)
                        {
                            pdu.capsFlags = in.U32();
                        }
                        break;
                    }
                    default:
                        break;
                }
                return in.Ok();
            }
        } // namespace

        bool ParseGfxPdus(const Bytes& message, std::vector<GfxPdu>& out, std::string& error)
        {
            out.clear();
            size_t p = 0;
            while (p < message.size())
            {
                if (message.size() - p < kHeaderSize)
                {
                    error = "a graphics message too short for its header";
                    return false;
                }
                uint16_t command = static_cast<uint16_t>(message[p] | (message[p + 1] << 8));
                uint32_t length = static_cast<uint32_t>(message[p + 4]) |
                                  (static_cast<uint32_t>(message[p + 5]) << 8) |
                                  (static_cast<uint32_t>(message[p + 6]) << 16) |
                                  (static_cast<uint32_t>(message[p + 7]) << 24);
                if (length < kHeaderSize || length > message.size() - p)
                {
                    error = "a graphics message with a bad length";
                    return false;
                }
                GfxPdu pdu;
                pdu.command = static_cast<GfxCommand>(command);
                Reader in(message.data() + p + kHeaderSize, length - kHeaderSize);
                if (!ParseBody(pdu.command, in, pdu))
                {
                    char text[96];
                    snprintf(text, sizeof(text), "a graphics message (command 0x%04x) shorter than its contents",
                             command);
                    error = text;
                    return false;
                }
                out.push_back(std::move(pdu));
                p += length;
            }
            return true;
        }

        Bytes BuildGfxCapsAdvertise()
        {
            // Newest first. H.264 is turned off where the version allows it; scaled output mapping too, so a
            // surface is always drawn at its own size. Version 8.0 and 8.1 sets (8.1 without its H.264 flag) are
            // there for servers that know nothing newer.
            struct CapSet
            {
                uint32_t version;
                uint32_t flags;
            };
            const CapSet sets[] = {
                {kGfxCapVersion107, kGfxCapsAvcDisabled | kGfxCapsScaledMapDisable},
                {kGfxCapVersion10, kGfxCapsAvcDisabled},
                {kGfxCapVersion81, 0},
                {kGfxCapVersion8, 0},
            };
            Bytes body;
            PutU16(body, static_cast<uint16_t>(sizeof(sets) / sizeof(sets[0])));
            for (const CapSet& set : sets)
            {
                PutU32(body, set.version);
                PutU32(body, 4); // capsDataLength: just the flags
                PutU32(body, set.flags);
            }
            Bytes out;
            PutU16(out, static_cast<uint16_t>(GfxCommand::CapsAdvertise));
            PutU16(out, 0);
            PutU32(out, static_cast<uint32_t>(kHeaderSize + body.size()));
            out.insert(out.end(), body.begin(), body.end());
            return out;
        }

        Bytes BuildGfxFrameAcknowledge(uint32_t frameId, uint32_t totalFramesDecoded)
        {
            Bytes out;
            PutU16(out, static_cast<uint16_t>(GfxCommand::FrameAcknowledge));
            PutU16(out, 0);
            PutU32(out, kHeaderSize + 12);
            PutU32(out, 0); // queueDepth: QUEUE_DEPTH_UNAVAILABLE
            PutU32(out, frameId);
            PutU32(out, totalFramesDecoded);
            return out;
        }

        std::string GfxCodecName(uint16_t codecId)
        {
            switch (static_cast<GfxCodec>(codecId))
            {
                case GfxCodec::Uncompressed:
                    return "uncompressed";
                case GfxCodec::RemoteFx:
                    return "RemoteFX";
                case GfxCodec::ClearCodec:
                    return "ClearCodec";
                case GfxCodec::Progressive:
                    return "RemoteFX Progressive";
                case GfxCodec::Planar:
                    return "planar";
                case GfxCodec::Avc420:
                    return "H.264 (AVC420)";
                case GfxCodec::Alpha:
                    return "alpha";
                case GfxCodec::ProgressiveV2:
                    return "RemoteFX Progressive v2";
                case GfxCodec::Avc444:
                    return "H.264 (AVC444)";
                case GfxCodec::Avc444v2:
                    return "H.264 (AVC444v2)";
            }
            char text[32];
            snprintf(text, sizeof(text), "codec 0x%04x", codecId);
            return text;
        }
    } // namespace Rdp
} // namespace Marconi
