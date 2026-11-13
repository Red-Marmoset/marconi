// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Choosing the platform's video decoder

#include <MarconiCore/Video/VideoDecoder.h>

#include <algorithm>

namespace Marconi
{
    namespace Video
    {
        std::vector<Wire::VideoCodec> PlatformDecodableCodecs()
        {
            std::vector<Wire::VideoCodec> codecs;
            auto add = [&codecs](const std::vector<Wire::VideoCodec>& more) {
                for (Wire::VideoCodec codec : more)
                {
                    if (std::find(codecs.begin(), codecs.end(), codec) == codecs.end())
                    {
                        codecs.push_back(codec);
                    }
                }
            };
#if defined(_WIN32)
            add(WindowsDecodableCodecs());
#endif
#if defined(MARCONI_VIDEO_MAC)
            add(MacDecodableCodecs());
#endif
#if defined(MARCONI_VIDEO_FFMPEG)
            add(FfmpegDecodableCodecs());
#endif
            (void)add; // no backend on some platforms (Linux without ffmpeg): the list stays empty
            return codecs;
        }

        std::unique_ptr<VideoDecoder> CreatePlatformVideoDecoder(Wire::VideoCodec codec)
        {
            std::unique_ptr<VideoDecoder> decoder;
#if defined(_WIN32)
            decoder = CreateWindowsVideoDecoder(codec);
            if (decoder)
            {
                return decoder;
            }
#endif
#if defined(MARCONI_VIDEO_MAC)
            decoder = CreateMacVideoDecoder(codec);
            if (decoder)
            {
                return decoder;
            }
#endif
#if defined(MARCONI_VIDEO_FFMPEG)
            decoder = CreateFfmpegVideoDecoder(codec);
            if (decoder)
            {
                return decoder;
            }
#endif
            (void)codec;
            return decoder;
        }
    } // namespace Video
} // namespace Marconi
