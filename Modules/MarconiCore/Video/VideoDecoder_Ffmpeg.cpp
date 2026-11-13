// Copyright (c) 2026 Red Marmoset Studios Ltd.
// H.264 decoding with FFmpeg (libavcodec), for any platform that has the engine's `ffmpeg` external dependency.
// Built and tested on Linux only (the macOS recipe in the engine's ffmpeg.manifest has not been run).
//
// FFmpeg is used as static libraries under the LGPL: see the engine's Documentation/FFmpeg.md for what shipping it
// requires. Nothing here touches GPL parts (the engine's build has none).

#include "VideoDecoder_Ffmpeg.h"

#include <MarconiCore/Video/VideoDecoder.h>

#if defined(UNA_HAVE_FFMPEG)

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Video
    {
        namespace
        {
            std::string FfmpegErrorText(int code)
            {
                char text[AV_ERROR_MAX_STRING_SIZE] = {};
                av_strerror(code, text, sizeof(text));
                return text;
            }

            class FfmpegDecoder : public FfmpegCore
            {
            public:
                FfmpegDecoder() = default;

                ~FfmpegDecoder() override
                {
                    sws_freeContext(m_scaler);
                    av_frame_free(&m_frame);
                    av_packet_free(&m_packet);
                    avcodec_free_context(&m_context);
                }

                bool Open()
                {
                    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
                    if (codec == nullptr)
                    {
                        return false;
                    }
                    m_context = avcodec_alloc_context3(codec);
                    m_packet = av_packet_alloc();
                    m_frame = av_frame_alloc();
                    if (m_context == nullptr || m_packet == nullptr || m_frame == nullptr)
                    {
                        return false;
                    }
                    // Low latency: show each picture as soon as it is decoded. Slice threading adds no delay (frame
                    // threading would add a picture of delay a thread).
                    m_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
                    m_context->flags2 |= AV_CODEC_FLAG2_FAST;
                    m_context->thread_type = FF_THREAD_SLICE;
                    m_context->thread_count = static_cast<int>(std::min(4u, std::max(1u, std::thread::hardware_concurrency())));
                    if (avcodec_open2(m_context, codec, nullptr) < 0)
                    {
                        return false;
                    }
                    return true;
                }

                bool Decode(const uint8_t* data, size_t size, const FrameCallback& onFrame, std::string* error) override
                {
                    if (data == nullptr || size == 0)
                    {
                        return true;
                    }
                    // The packet's data is only read by the decoder (it is not freed or changed), so it can point
                    // straight at the caller's bytes. The decoder needs AV_INPUT_BUFFER_PADDING_SIZE readable bytes
                    // after the end of its input, which the caller's buffer does not promise, so copy.
                    m_padded.assign(data, data + size);
                    m_padded.resize(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
                    m_packet->data = m_padded.data();
                    m_packet->size = static_cast<int>(size);

                    int result = avcodec_send_packet(m_context, m_packet);
                    if (result < 0 && result != AVERROR(EAGAIN))
                    {
                        return Fail("decoder refused the data: " + FfmpegErrorText(result), error);
                    }

                    bool damaged = false;
                    for (;;)
                    {
                        result = avcodec_receive_frame(m_context, m_frame);
                        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                        {
                            break;
                        }
                        if (result < 0)
                        {
                            return Fail("decoder failed: " + FfmpegErrorText(result), error);
                        }
                        const bool isKey = (m_frame->flags & AV_FRAME_FLAG_KEY) != 0;
                        if (m_frame->decode_error_flags != 0 || (m_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0)
                        {
                            damaged = true; // keep draining, then reset
                        }
                        else if (m_needKeyframe && !isKey)
                        {
                            // A picture that builds on one we never had: do not show it.
                        }
                        else if (!Convert(*m_frame))
                        {
                            av_frame_unref(m_frame);
                            return Fail("could not convert the picture", error);
                        }
                        else
                        {
                            if (isKey)
                            {
                                m_needKeyframe = false;
                            }
                            DecodedFrame out;
                            out.width = m_frame->width;
                            out.height = m_frame->height;
                            out.pixels = m_pixels.data();
                            out.isKeyframe = isKey;
                            if (onFrame)
                            {
                                onFrame(out);
                            }
                        }
                        av_frame_unref(m_frame);
                    }
                    if (damaged)
                    {
                        return Fail("the picture was damaged", error);
                    }
                    return true;
                }

                bool NeedsKeyframe() const override { return m_needKeyframe; }

                void Reset() override
                {
                    avcodec_flush_buffers(m_context);
                    m_needKeyframe = true;
                }

                const char* Name() const override { return "FFmpeg H.264 (software)"; }

            private:
                bool Fail(const std::string& message, std::string* error)
                {
                    if (error != nullptr)
                    {
                        *error = message;
                    }
                    Reset();
                    return false;
                }

                // Converts the decoded picture (any YUV layout) to 0x00RRGGBB into m_pixels.
                bool Convert(const AVFrame& frame)
                {
                    if (frame.width <= 0 || frame.height <= 0)
                    {
                        return false;
                    }
                    m_scaler = sws_getCachedContext(m_scaler, frame.width, frame.height,
                                                    static_cast<AVPixelFormat>(frame.format), frame.width,
                                                    frame.height, AV_PIX_FMT_BGR0, SWS_BILINEAR, nullptr, nullptr,
                                                    nullptr);
                    if (m_scaler == nullptr)
                    {
                        return false;
                    }
                    // Use the matrix and range the stream says; unspecified means BT.601 limited range, FFmpeg's own
                    // default. (The server must tag its stream: see SERVER_PLAN.md.)
                    int colorSpace = SWS_CS_DEFAULT;
                    switch (frame.colorspace)
                    {
                    case AVCOL_SPC_BT709:
                        colorSpace = SWS_CS_ITU709;
                        break;
                    case AVCOL_SPC_BT470BG:
                    case AVCOL_SPC_SMPTE170M:
                        colorSpace = SWS_CS_ITU601;
                        break;
                    case AVCOL_SPC_BT2020_NCL:
                        colorSpace = SWS_CS_BT2020;
                        break;
                    default:
                        break;
                    }
                    const int sourceFull = frame.color_range == AVCOL_RANGE_JPEG ? 1 : 0;
                    const int* inverse = sws_getCoefficients(colorSpace);
                    const int* table = sws_getCoefficients(SWS_CS_DEFAULT);
                    sws_setColorspaceDetails(m_scaler, inverse, sourceFull, table, 1, 0, 1 << 16, 1 << 16);

                    m_pixels.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height));
                    uint8_t* destination[4] = {reinterpret_cast<uint8_t*>(m_pixels.data()), nullptr, nullptr, nullptr};
                    int destinationStride[4] = {frame.width * 4, 0, 0, 0};
                    const int rows = sws_scale(m_scaler, frame.data, frame.linesize, 0, frame.height, destination,
                                               destinationStride);
                    if (rows != frame.height)
                    {
                        return false;
                    }
                    // BGR0 leaves the fourth byte undefined: clear it so pixels are exactly 0x00RRGGBB.
                    for (uint32_t& pixel : m_pixels)
                    {
                        pixel &= 0x00FFFFFFu;
                    }
                    return true;
                }

                AVCodecContext* m_context = nullptr;
                AVPacket* m_packet = nullptr;
                AVFrame* m_frame = nullptr;
                SwsContext* m_scaler = nullptr;
                std::vector<uint8_t> m_padded;
                std::vector<uint32_t> m_pixels;
                bool m_needKeyframe = true;
            };
        } // namespace

        bool FfmpegAvailable()
        {
            return true;
        }

        std::unique_ptr<FfmpegCore> CreateFfmpegDecoder()
        {
            std::unique_ptr<FfmpegDecoder> decoder(new FfmpegDecoder());
            if (!decoder->Open())
            {
                return nullptr;
            }
            return decoder;
        }

#if defined(MARCONI_VIDEO_FFMPEG)
        namespace
        {
            // The viewer's interface over the core: one picture comes out of the call that gave its data.
            class FfmpegVideoDecoder : public VideoDecoder
            {
            public:
                const char* Name() const override { return "libavcodec h264"; }

                bool Configure(const DecoderStreamInfo& info, std::string& error) override
                {
                    if (info.codec != Wire::VideoCodec::H264)
                    {
                        error = "this decoder only does H.264";
                        return false;
                    }
                    m_core = CreateFfmpegDecoder();
                    if (m_core == nullptr)
                    {
                        error = "FFmpeg's H.264 decoder could not be started";
                        return false;
                    }
                    m_core->Reset();
                    if (!info.extradata.empty())
                    {
                        // The parameter sets (SPS and PPS) in front of the first picture; they make no picture.
                        std::string ignored;
                        m_core->Decode(info.extradata.data(), info.extradata.size(), nullptr, &ignored);
                    }
                    return true;
                }

                DecodeStatus Decode(const uint8_t* data, size_t size, bool /*keyframe*/, DecodedPicture& out,
                                    std::string& error) override
                {
                    if (m_core == nullptr)
                    {
                        error = "the decoder was not configured";
                        return DecodeStatus::Failed;
                    }
                    bool produced = false;
                    std::string coreError;
                    const bool ok = m_core->Decode(
                        data, size,
                        [&](const DecodedFrame& frame) {
                            out.width = frame.width;
                            out.height = frame.height;
                            out.pixels.assign(frame.pixels,
                                              frame.pixels + static_cast<size_t>(frame.width) * frame.height);
                            produced = true;
                        },
                        &coreError);
                    if (!ok)
                    {
                        error = coreError.empty() ? "the picture could not be decoded" : coreError;
                        return DecodeStatus::Failed;
                    }
                    if (produced)
                    {
                        return DecodeStatus::Picture;
                    }
                    if (m_core->NeedsKeyframe())
                    {
                        // A picture that builds on one never seen is dropped: tell the viewer, which asks for a keyframe.
                        error = "waiting for a keyframe";
                        return DecodeStatus::Failed;
                    }
                    return DecodeStatus::NeedMoreInput;
                }

                void Reset() override
                {
                    if (m_core != nullptr)
                    {
                        m_core->Reset();
                    }
                }

            private:
                std::unique_ptr<FfmpegCore> m_core;
            };
        } // namespace

        std::vector<Wire::VideoCodec> FfmpegDecodableCodecs()
        {
            return {Wire::VideoCodec::H264};
        }

        std::unique_ptr<VideoDecoder> CreateFfmpegVideoDecoder(Wire::VideoCodec codec)
        {
            if (codec != Wire::VideoCodec::H264)
            {
                return nullptr;
            }
            return std::unique_ptr<VideoDecoder>(new FfmpegVideoDecoder());
        }
#endif
    } // namespace Video
} // namespace Marconi

#else // no FFmpeg in this build

namespace Marconi
{
    namespace Video
    {
        bool FfmpegAvailable()
        {
            return false;
        }

        std::unique_ptr<FfmpegCore> CreateFfmpegDecoder()
        {
            return nullptr;
        }
    } // namespace Video
} // namespace Marconi

#endif
