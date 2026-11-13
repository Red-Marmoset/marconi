// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the server needs from a video encoder, and where the platform encoders plug in
//
// An encoder turns the screen (a Capture and the area of it being streamed) into one encoded picture. The
// VideoSession decides when to encode, what to do with the rate and when to ask for a keyframe; an encoder only encodes.
//
// Backends:
//   VideoEncoder_Windows.cpp   Media Foundation H.264 (hardware MFT, software fallback)   (written, NOT yet built
//                              on Windows)
//   (tests)                    Tests/MarconiServerTest/TestVideoCodec.cpp: the test codec, never in a product.
// macOS (VideoToolbox) and Linux (VAAPI, x264) encoders are not written.
//
// Threading: used from the session's frame thread only.

#pragma once

#include "ScreenSource.h"

#include <MarconiCore/Video/VideoBackendSwitch.h>
#include <MarconiCore/Wire/WireVideoMessages.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        struct VideoEncoderConfig
        {
            Wire::VideoCodec codec = Wire::VideoCodec::H264;
            uint32_t width = 0;  // the picture's size: the area of the desktop being streamed
            uint32_t height = 0; // (a backend that needs even sizes pads internally; the stream keeps these numbers)
            uint32_t fps = 60;
            uint32_t bitrateKbps = 8000;
            // Frames between keyframes, so a viewer that joins or loses its place recovers without asking (an
            // encoder may use intra refresh instead). 0 = keyframes only when asked for.
            uint32_t keyframeInterval = 240;
        };

        struct EncodedPicture
        {
            Wire::Bytes data;
            bool keyframe = false;
            uint32_t encodeMicros = 0; // how long the encoder took, as it measured it
        };

        enum class EncodeStatus
        {
            Picture, // `out` holds the encoded picture
            NoOutput, // the encoder kept the frame and has nothing yet (it should not, in low-latency mode)
            Failed
        };

        class VideoEncoder
        {
        public:
            virtual ~VideoEncoder() = default;

            // A short name for the log ("Media Foundation (hardware: NVIDIA)", "test codec").
            virtual const char* Name() const = 0;
            virtual bool IsHardware() const { return false; }

            // Starts (or restarts, for a new size) encoding. False, with a sentence in `error`, if it can't be done.
            // Low-latency settings are the rule: no B-frames, no look-ahead, one frame in flight, CBR or tight VBR.
            virtual bool Configure(const VideoEncoderConfig& config, std::string& error) = 0;

            // The codec set-up bytes the viewer's decoder needs before the first frame (H.264: the SPS and PPS in
            // Annex B), or empty when the stream carries them in-band on every keyframe. Valid after Configure.
            virtual Wire::Bytes GetExtradata() const { return {}; }

            // Encodes the part of `capture` inside `area` (desktop coordinates; always the size Configure was given).
            // The next picture is a keyframe if ForceKeyframe was called since the last one.
            virtual EncodeStatus Encode(const Capture& capture, const Wire::Rect& area, EncodedPicture& out,
                                        std::string& error) = 0;

            // The next picture encoded is a keyframe (a whole picture that decodes alone).
            virtual void ForceKeyframe() = 0;

            // Changes the target bitrate without restarting the stream.
            virtual void SetBitrate(uint32_t kbps) = 0;

            // Pictures the encoder still holds, oldest first (empty for a low-latency encoder). Called before it is
            // destroyed or reconfigured.
            virtual void Flush(std::vector<EncodedPicture>& out) { out.clear(); }
        };

        using VideoEncoderFactory = std::function<std::unique_ptr<VideoEncoder>(Wire::VideoCodec)>;

        // The codecs this platform's encoders can do, best first, and an encoder for one (null if none). On Linux and
        // macOS there are none yet, so the server doesn't offer video there and viewers get still pictures.
        std::vector<Wire::VideoCodec> PlatformEncodableCodecs();
        std::unique_ptr<VideoEncoder> CreatePlatformVideoEncoder(Wire::VideoCodec codec);

#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)
        // Implemented by VideoEncoder_Windows.cpp (not yet built on Windows).
        std::vector<Wire::VideoCodec> WindowsEncodableCodecs();
        std::unique_ptr<VideoEncoder> CreateWindowsVideoEncoder(Wire::VideoCodec codec);
#endif
    } // namespace Server
} // namespace Marconi
