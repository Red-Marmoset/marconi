// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Choosing the platform's video encoder

#include "VideoEncoder.h"

namespace Marconi
{
    namespace Server
    {
        std::vector<Wire::VideoCodec> PlatformEncodableCodecs()
        {
#if defined(_WIN32)
            return WindowsEncodableCodecs();
#else
            return {};
#endif
        }

        std::unique_ptr<VideoEncoder> CreatePlatformVideoEncoder(Wire::VideoCodec codec)
        {
#if defined(_WIN32)
            return CreateWindowsVideoEncoder(codec);
#else
            (void)codec;
            return nullptr;
#endif
        }
    } // namespace Server
} // namespace Marconi
