// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The FFmpeg H.264 decoder's own interface: a callback for each picture, and a flag that says it is waiting for a
// keyframe. VideoDecoder_Ffmpeg.cpp wraps it in the viewer's `VideoDecoder` interface (VideoDecoder.h), which is what
// the rest of the program uses; this is kept separate so the decoder itself can be tested directly.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Video
    {
        // One decoded picture in the viewer's pixel format: 32 bits a pixel, 0x00RRGGBB (blue in the low byte).
        struct DecodedFrame
        {
            int width = 0;
            int height = 0;
            const uint32_t* pixels = nullptr; // width * height pixels, rows with no padding; valid only during the callback
            bool isKeyframe = false;
        };

        class FfmpegCore
        {
        public:
            using FrameCallback = std::function<void(const DecodedFrame&)>;

            virtual ~FfmpegCore() = default;

            // Decodes one access unit (one picture's data, H.264 in Annex B form: start codes, with the SPS and PPS
            // in front of every keyframe). Calls `onFrame` for each picture ready, which is one picture straight
            // away: the decoder holds no pictures back to reorder them.
            // Returns false when the stream is damaged: the decoder has reset itself, `NeedsKeyframe()` is true and
            // the caller must ask the server for a keyframe. `error` (if not null) says what went wrong.
            virtual bool Decode(const uint8_t* data, size_t size, const FrameCallback& onFrame,
                                std::string* error = nullptr) = 0;

            // True from a failure (or the start) until a keyframe has been decoded. Pictures that need earlier ones
            // are dropped meanwhile instead of being shown wrong. The caller may ask again for a keyframe if this
            // stays true for a while.
            virtual bool NeedsKeyframe() const = 0;

            // Forgets everything (after a reconnect, say). NeedsKeyframe() is true afterwards.
            virtual void Reset() = 0;

            virtual const char* Name() const = 0;
        };
    } // namespace Video
} // namespace Marconi
