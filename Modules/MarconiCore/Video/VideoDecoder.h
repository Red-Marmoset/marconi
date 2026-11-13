// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the viewer needs from a video decoder, and where the platform decoders plug in
//
// A decoder turns one encoded picture (a VideoFrame's payload) into pixels. The viewer's VideoReceiver owns the
// stream logic (which frame is next, when to ask for a keyframe, which rectangles to copy); a decoder only decodes.
//
// Backends (one file each, picked by `CreatePlatformVideoDecoder` in VideoDecoderFactory.cpp):
//   VideoDecoder_Windows.cpp   Media Foundation H.264 decoder        (written, NOT yet built on Windows)
//   VideoDecoder_Mac.cpp       VideoToolbox                          (written, NOT yet built on a Mac)
//   VideoDecoder_Ffmpeg.cpp    libavcodec, for Linux and as a fallback elsewhere: added by branch claude/ffmpeg.
//                              What is expected of it is written in SERVER_PLAN.md ("The ffmpeg decoder slot").
//   (tests)                    Tests/MarconiServerTest/TestVideoCodec.cpp: the test codec, never in a product.
//
// Threading: a decoder is used from one thread at a time (the client's worker thread). Calls block until done.

#pragma once

#include <MarconiCore/Wire/WireVideoMessages.h>

#include <MarconiCore/Video/VideoBackendSwitch.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Video
    {
        // One decoded picture: 0x00RRGGBB pixels, one uint32 each, row by row, no padding (the same layout as the
        // Framebuffer and a Still). GPU textures come later and will be a second output kind; today every backend
        // converts to this on the CPU.
        struct DecodedPicture
        {
            int32_t width = 0;
            int32_t height = 0;
            std::vector<uint32_t> pixels; // width*height
        };

        // What a decoder is told about the stream, from VideoStart.
        struct DecoderStreamInfo
        {
            Wire::VideoCodec codec = Wire::VideoCodec::None;
            uint32_t width = 0;
            uint32_t height = 0;
            Wire::Bytes extradata; // codec set-up bytes (H.264: SPS and PPS in Annex B); may be empty
        };

        enum class DecodeStatus
        {
            Picture,       // `out` holds the picture for the frame just given
            NeedMoreInput, // the decoder kept the data and has no picture yet (a decoder with delay)
            Failed         // the data was not decodable; the viewer asks for a keyframe and Reset()s
        };

        class VideoDecoder
        {
        public:
            virtual ~VideoDecoder() = default;

            // A short name for the log ("Media Foundation (hardware)", "libavcodec h264").
            virtual const char* Name() const = 0;

            // Starts (or restarts) decoding a stream. False, with a sentence in `error`, if this decoder can't do it.
            virtual bool Configure(const DecoderStreamInfo& info, std::string& error) = 0;

            // Decodes one frame. `keyframe` says it decodes without earlier frames. A low-latency decoder returns
            // Picture for every frame given; one that buffers returns NeedMoreInput until it has one (the viewer
            // then shows it a frame late, which is a delay and not an error). On Failed, `error` says why.
            virtual DecodeStatus Decode(const uint8_t* data, size_t size, bool keyframe, DecodedPicture& out,
                                        std::string& error) = 0;

            // Forgets every earlier frame: the next frame given must be a keyframe. The configuration stays.
            virtual void Reset() = 0;
        };

        // Makes a decoder for a codec, or null if there is none. The viewer takes one of these so a test (or a
        // build with its own backend) can stand in for the platform's.
        using VideoDecoderFactory = std::function<std::unique_ptr<VideoDecoder>(Wire::VideoCodec)>;

        // The codecs this platform's decoders can do, best first, and a decoder for one of them (null if none).
        // The Windows backend, the Mac backend and (when claude/ffmpeg lands it) the ffmpeg one are tried in that
        // order of preference for the platform; on Linux with no ffmpeg both are empty and the viewer simply
        // doesn't offer video, so the server sends Still rectangles.
        std::vector<Wire::VideoCodec> PlatformDecodableCodecs();
        std::unique_ptr<VideoDecoder> CreatePlatformVideoDecoder(Wire::VideoCodec codec);

        // The backends' entry points. Each returns null when it can't decode `codec`. They are declared here so
        // VideoDecoderFactory.cpp can reach them; only the ones whose file is built on this platform exist.
#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)
        std::vector<Wire::VideoCodec> WindowsDecodableCodecs();
        std::unique_ptr<VideoDecoder> CreateWindowsVideoDecoder(Wire::VideoCodec codec);
#endif
#if defined(MARCONI_VIDEO_USE_VIDEOTOOLBOX)
        std::vector<Wire::VideoCodec> MacDecodableCodecs();
        std::unique_ptr<VideoDecoder> CreateMacVideoDecoder(Wire::VideoCodec codec);
#endif
#if defined(MARCONI_VIDEO_FFMPEG)
        // Implemented by VideoDecoder_Ffmpeg.cpp (branch claude/ffmpeg), which also defines MARCONI_VIDEO_FFMPEG
        // for this module's build. See SERVER_PLAN.md for what it must do.
        std::vector<Wire::VideoCodec> FfmpegDecodableCodecs();
        std::unique_ptr<VideoDecoder> CreateFfmpegVideoDecoder(Wire::VideoCodec codec);
#endif
    } // namespace Video
} // namespace Marconi
