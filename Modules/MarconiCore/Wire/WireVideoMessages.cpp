// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the video messages into bytes and back

#include <MarconiCore/Wire/WireVideoMessages.h>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            void WriteRect(Writer& w, const Rect& rect)
            {
                w.S32(rect.x).S32(rect.y).S32(rect.width).S32(rect.height);
            }

            Rect ReadRect(Reader& r)
            {
                Rect rect;
                rect.x = r.S32();
                rect.y = r.S32();
                rect.width = r.S32();
                rect.height = r.S32();
                return rect;
            }

            uint32_t ReadDimension(Reader& r, bool allowZero)
            {
                uint32_t value = r.U32();
                if (value > kMaxVideoDimension || (!allowZero && value == 0))
                {
                    throw WireError("a video size is out of range");
                }
                return value;
            }
        } // namespace

        const char* VideoCodecName(VideoCodec codec)
        {
            switch (codec)
            {
            case VideoCodec::H264: return "H.264";
            case VideoCodec::Hevc: return "HEVC";
            case VideoCodec::Av1: return "AV1";
            case VideoCodec::Test: return "test codec";
            default: return "none";
            }
        }

        Bytes VideoOffer::Encode() const
        {
            return Build(Type::VideoOffer, [&](Writer& w) {
                w.Varint(codecs.size());
                for (VideoCodec codec : codecs)
                {
                    w.U8(static_cast<uint8_t>(codec));
                }
                w.U32(maxWidth).U32(maxHeight).U16(maxFps);
            });
        }

        VideoOffer VideoOffer::Decode(Reader& r)
        {
            VideoOffer message;
            uint64_t count = r.Varint();
            if (count > kMaxVideoCodecs)
            {
                throw WireError("a video offer lists too many codecs");
            }
            for (uint64_t i = 0; i < count; ++i)
            {
                message.codecs.push_back(static_cast<VideoCodec>(r.U8()));
            }
            message.maxWidth = ReadDimension(r, true);
            message.maxHeight = ReadDimension(r, true);
            message.maxFps = r.U16();
            return message;
        }

        Bytes VideoStart::Encode() const
        {
            return Build(Type::VideoStart, [&](Writer& w) {
                w.U32(streamId).U8(static_cast<uint8_t>(codec)).U32(width).U32(height).U16(fps).Varint(bitrateKbps);
                WriteRect(w, origin);
                w.Blob(extradata);
            });
        }

        VideoStart VideoStart::Decode(Reader& r)
        {
            VideoStart message;
            message.streamId = r.U32();
            message.codec = static_cast<VideoCodec>(r.U8());
            message.width = ReadDimension(r, false);
            message.height = ReadDimension(r, false);
            message.fps = r.U16();
            message.bitrateKbps = static_cast<uint32_t>(r.Varint());
            message.origin = ReadRect(r);
            message.extradata = r.Blob();
            if (message.extradata.size() > kMaxVideoExtradata)
            {
                throw WireError("video set-up bytes are too long");
            }
            return message;
        }

        Bytes VideoFrame::Encode() const
        {
            return Build(Type::VideoFrame, [&](Writer& w) {
                w.U32(streamId).U64(frameId).U8(flags).U64(captureMicros).U32(encodeMicros);
                w.Varint(applyRects.size());
                for (const Rect& rect : applyRects)
                {
                    WriteRect(w, rect);
                }
                w.Blob(payload);
            });
        }

        VideoFrame VideoFrame::Decode(Reader& r)
        {
            VideoFrame message;
            message.streamId = r.U32();
            message.frameId = r.U64();
            message.flags = r.U8();
            message.captureMicros = r.U64();
            message.encodeMicros = r.U32();
            uint64_t count = r.Varint();
            if (count > kMaxVideoApplyRects)
            {
                throw WireError("a video frame lists too many rectangles");
            }
            for (uint64_t i = 0; i < count; ++i)
            {
                message.applyRects.push_back(ReadRect(r));
            }
            message.payload = r.Blob();
            return message;
        }

        Bytes VideoRequestKeyframe::Encode() const
        {
            return Build(Type::VideoRequestKeyframe, [&](Writer& w) {
                w.U32(streamId).U8(static_cast<uint8_t>(reason)).U64(lastGoodFrameId);
            });
        }

        VideoRequestKeyframe VideoRequestKeyframe::Decode(Reader& r)
        {
            VideoRequestKeyframe message;
            message.streamId = r.U32();
            message.reason = static_cast<Reason>(r.U8());
            message.lastGoodFrameId = r.U64();
            return message;
        }

        Bytes VideoFeedback::Encode() const
        {
            return Build(Type::VideoFeedback, [&](Writer& w) {
                w.U32(streamId).U64(lastFrameId).U32(framesReceived).U32(framesDropped).U32(decodeMicros);
                w.U8(queueDepth).Varint(targetBitrateKbps).U16(maxFps).U8(quality);
            });
        }

        VideoFeedback VideoFeedback::Decode(Reader& r)
        {
            VideoFeedback message;
            message.streamId = r.U32();
            message.lastFrameId = r.U64();
            message.framesReceived = r.U32();
            message.framesDropped = r.U32();
            message.decodeMicros = r.U32();
            message.queueDepth = r.U8();
            message.targetBitrateKbps = static_cast<uint32_t>(r.Varint());
            message.maxFps = r.U16();
            message.quality = r.U8();
            if (message.quality > 100)
            {
                throw WireError("a video quality wish is out of range");
            }
            return message;
        }

        Bytes VideoStop::Encode() const
        {
            return Build(Type::VideoStop, [&](Writer& w) { w.U32(streamId).U8(static_cast<uint8_t>(reason)); });
        }

        VideoStop VideoStop::Decode(Reader& r)
        {
            VideoStop message;
            message.streamId = r.U32();
            message.reason = static_cast<Reason>(r.U8());
            return message;
        }
    } // namespace Wire
} // namespace Marconi
