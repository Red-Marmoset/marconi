// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Choosing the platform's video encoder

#include "VideoEncoder.h"

namespace Marconi
{
    namespace Server
    {
        std::vector<Wire::VideoCodec> PlatformEncodableCodecs()
        {
#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)
            return WindowsEncodableCodecs();
#else
            return {};
#endif
        }

        std::unique_ptr<VideoEncoder> CreatePlatformVideoEncoder(Wire::VideoCodec codec)
        {
#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)
            return CreateWindowsVideoEncoder(codec);
#else
            (void)codec;
            return nullptr;
#endif
        }
    } // namespace Server
} // namespace Marconi
