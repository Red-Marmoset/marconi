// Copyright (c) 2026 Red Marmoset Studios Ltd.
// H.264 decoding with FFmpeg (libavcodec), for any platform that has the engine's `ffmpeg` external dependency

#pragma once

#include "VideoDecoder_Local.h"

#include <memory>

namespace Marconi
{
    namespace Video
    {
        // True when this build has FFmpeg (the engine's ffmpeg external defines UNA_HAVE_FFMPEG on Linux and macOS).
        bool FfmpegAvailable();

        // A low-latency software H.264 decoder, or null when FFmpeg is not in this build.
        std::unique_ptr<VideoDecoder> CreateFfmpegDecoder();
    } // namespace Video
} // namespace Marconi
