// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the FFmpeg H.264 decoder: a known picture, one picture a call with no delay, damage and recovery at the
// next keyframe, a change of picture size

#include "TestSupport.h"

#include "VideoTestClip.h"
#include "VideoTestClipSmall.h"

#include <MarconiCore/Video/VideoDecoder_Ffmpeg.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace MarconiTest
{
    namespace
    {
        using Bytes = std::vector<uint8_t>;

        // Splits an Annex B stream into access units: a new one starts at a slice that follows a slice, or at the
        // SPS/PPS/SEI that comes after a slice. (The clips have one slice a picture.)
        std::vector<Bytes> SplitAccessUnits(const uint8_t* data, size_t size)
        {
            std::vector<size_t> starts; // offset of each start code (00 00 01 or 00 00 00 01)
            for (size_t i = 0; i + 3 < size; ++i)
            {
                if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
                {
                    starts.push_back(i > 0 && data[i - 1] == 0 ? i - 1 : i);
                    i += 2;
                }
            }
            std::vector<Bytes> units;
            Bytes current;
            bool haveSlice = false;
            for (size_t n = 0; n < starts.size(); ++n)
            {
                const size_t begin = starts[n];
                const size_t end = n + 1 < starts.size() ? starts[n + 1] : size;
                size_t header = begin;
                while (header < end && data[header] == 0)
                {
                    ++header;
                }
                ++header; // the 01
                const int type = header < end ? (data[header] & 0x1F) : 0;
                const bool isSlice = type == 1 || type == 5;
                if (haveSlice && (isSlice || type == 6 || type == 7 || type == 8 || type == 9))
                {
                    units.push_back(current);
                    current.clear();
                    haveSlice = false;
                }
                current.insert(current.end(), data + begin, data + end);
                haveSlice = haveSlice || isSlice;
            }
            if (!current.empty())
            {
                units.push_back(current);
            }
            return units;
        }

        struct Picture
        {
            int width = 0;
            int height = 0;
            bool isKeyframe = false;
            bool highByteClear = true;
            std::vector<uint32_t> pixels;
        };

        // Decodes one access unit; returns the pictures it produced and whether Decode succeeded.
        bool DecodeUnit(Marconi::Video::VideoDecoder& decoder, const Bytes& unit, std::vector<Picture>& pictures,
                        std::string* error = nullptr)
        {
            return decoder.Decode(
                unit.data(), unit.size(),
                [&](const Marconi::Video::DecodedFrame& frame) {
                    Picture picture;
                    picture.width = frame.width;
                    picture.height = frame.height;
                    picture.isKeyframe = frame.isKeyframe;
                    picture.pixels.assign(frame.pixels, frame.pixels + static_cast<size_t>(frame.width) * frame.height);
                    for (uint32_t pixel : picture.pixels)
                    {
                        if ((pixel & 0xFF000000u) != 0)
                        {
                            picture.highByteClear = false;
                        }
                    }
                    pictures.push_back(std::move(picture));
                },
                error);
        }

        bool Near(uint32_t pixel, int r, int g, int b, int tolerance)
        {
            return abs(static_cast<int>((pixel >> 16) & 0xFF) - r) <= tolerance &&
                   abs(static_cast<int>((pixel >> 8) & 0xFF) - g) <= tolerance &&
                   abs(static_cast<int>(pixel & 0xFF) - b) <= tolerance;
        }

        // The red, green, blue and white quadrants, sampled well inside each, for a picture of any size.
        bool QuadrantsAreRight(const Picture& picture)
        {
            if (picture.width < 8 || picture.height < 8 ||
                picture.pixels.size() != static_cast<size_t>(picture.width) * picture.height)
            {
                return false;
            }
            auto at = [&](int x, int y) { return picture.pixels[static_cast<size_t>(y) * picture.width + x]; };
            const int w = picture.width;
            const int h = picture.height;
            const int tolerance = 16;
            return Near(at(w / 8, h / 8), 255, 0, 0, tolerance) && Near(at(w * 3 / 4, h / 8), 0, 255, 0, tolerance) &&
                   Near(at(w / 8, h * 5 / 6), 0, 0, 255, tolerance) &&
                   Near(at(w * 3 / 4, h * 5 / 6), 255, 255, 255, tolerance);
        }

        bool YellowSquareAt(const Picture& picture, int x, int y)
        {
            return Near(picture.pixels[static_cast<size_t>(y) * picture.width + x], 255, 255, 0, 40);
        }
    } // namespace

    void RunVideoDecoderFfmpegTests()
    {
        if (!Marconi::Video::FfmpegAvailable())
        {
            RunTest("Video decoder: FFmpeg not in this build", []() {
                CHECK(Marconi::Video::CreateFfmpegDecoder() == nullptr);
            });
            return;
        }

        const std::vector<Bytes> clip = SplitAccessUnits(VideoTestClip::g_clip, VideoTestClip::g_clipSize);

        RunTest("Video decoder: the clip splits into 12 pictures, keyframes at 0 and 6", []() {
            const std::vector<Bytes> units = SplitAccessUnits(VideoTestClip::g_clip, VideoTestClip::g_clipSize);
            CHECK(units.size() == 12);
        });

        RunTest("Video decoder: decodes the clip to the right colours, one picture a call, no delay", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            CHECK(decoder != nullptr);
            if (decoder == nullptr || clip.size() != 12)
            {
                CHECK(false);
                return;
            }
            CHECK(decoder->NeedsKeyframe());
            std::vector<Picture> all;
            for (size_t i = 0; i < clip.size(); ++i)
            {
                std::vector<Picture> now;
                std::string error;
                CHECK(DecodeUnit(*decoder, clip[i], now, &error));
                CHECK(now.size() == 1); // the picture comes out of the call that gave its data
                for (Picture& picture : now)
                {
                    CHECK(picture.width == 64 && picture.height == 48);
                    CHECK(picture.highByteClear);
                    CHECK(picture.isKeyframe == (i == 0 || i == 6));
                    CHECK(QuadrantsAreRight(picture));
                    all.push_back(std::move(picture));
                }
                CHECK(!decoder->NeedsKeyframe());
            }
            CHECK(all.size() == 12);
            if (all.size() == 12)
            {
                // The yellow square has moved right: 4 pixels a picture, 8 wide, in the rows 20..27.
                CHECK(!YellowSquareAt(all[0], 48, 24));
                CHECK(YellowSquareAt(all[11], 48, 24));
                CHECK(YellowSquareAt(all[5], 24, 24));
            }
        });

        RunTest("Video decoder: damaged data resets, asks for a keyframe, and recovers at the next one", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            CHECK(decoder != nullptr);
            if (decoder == nullptr || clip.size() != 12)
            {
                CHECK(false);
                return;
            }
            std::vector<Picture> pictures;
            CHECK(DecodeUnit(*decoder, clip[0], pictures)); // keyframe
            CHECK(pictures.size() == 1 && QuadrantsAreRight(pictures[0]));

            Bytes damaged = clip[1]; // a P picture with its slice data garbled
            for (size_t i = 12; i < damaged.size() && i < 24; ++i)
            {
                damaged[i] ^= 0xA5;
            }
            pictures.clear();
            std::string error;
            const bool ok = DecodeUnit(*decoder, damaged, pictures, &error);
            CHECK(!ok);
            CHECK(!error.empty());
            CHECK(pictures.empty()); // a damaged picture is not shown
            CHECK(decoder->NeedsKeyframe());

            // The pictures after it build on the damaged one: none is shown until the next keyframe.
            for (size_t i = 2; i < 6; ++i)
            {
                pictures.clear();
                DecodeUnit(*decoder, clip[i], pictures);
                CHECK(pictures.empty());
                CHECK(decoder->NeedsKeyframe());
            }
            pictures.clear();
            CHECK(DecodeUnit(*decoder, clip[6], pictures)); // keyframe: back to normal
            CHECK(pictures.size() == 1);
            CHECK(!decoder->NeedsKeyframe());
            if (pictures.size() == 1)
            {
                CHECK(pictures[0].isKeyframe);
                CHECK(QuadrantsAreRight(pictures[0]));
            }
            for (size_t i = 7; i < clip.size(); ++i)
            {
                pictures.clear();
                CHECK(DecodeUnit(*decoder, clip[i], pictures));
                CHECK(pictures.size() == 1 && QuadrantsAreRight(pictures[0]));
            }
        });

        RunTest("Video decoder: starting in the middle shows nothing until a keyframe", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            CHECK(decoder != nullptr);
            if (decoder == nullptr || clip.size() != 12)
            {
                CHECK(false);
                return;
            }
            std::vector<Picture> pictures;
            for (size_t i = 2; i < 6; ++i)
            {
                // The data has no SPS/PPS and no picture to build on: it may be refused, but nothing is shown.
                DecodeUnit(*decoder, clip[i], pictures);
            }
            CHECK(pictures.empty());
            CHECK(decoder->NeedsKeyframe());
            pictures.clear();
            CHECK(DecodeUnit(*decoder, clip[6], pictures));
            CHECK(pictures.size() == 1 && QuadrantsAreRight(pictures[0]));
            CHECK(!decoder->NeedsKeyframe());
        });

        RunTest("Video decoder: garbage in, no crash, recovers at a keyframe", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            CHECK(decoder != nullptr);
            if (decoder == nullptr || clip.size() != 12)
            {
                CHECK(false);
                return;
            }
            Bytes garbage(500);
            uint32_t seed = 12345;
            for (uint8_t& byte : garbage)
            {
                seed = seed * 1664525u + 1013904223u;
                byte = static_cast<uint8_t>(seed >> 24);
            }
            std::vector<Picture> pictures;
            DecodeUnit(*decoder, garbage, pictures);
            CHECK(pictures.empty());
            CHECK(decoder->NeedsKeyframe());
            CHECK(DecodeUnit(*decoder, clip[0], pictures));
            CHECK(pictures.size() == 1 && QuadrantsAreRight(pictures[0]));
            CHECK(!decoder->NeedsKeyframe());
        });

        RunTest("Video decoder: Reset asks for a keyframe again", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            CHECK(decoder != nullptr);
            if (decoder == nullptr || clip.size() != 12)
            {
                CHECK(false);
                return;
            }
            std::vector<Picture> pictures;
            CHECK(DecodeUnit(*decoder, clip[0], pictures));
            CHECK(!decoder->NeedsKeyframe());
            decoder->Reset();
            CHECK(decoder->NeedsKeyframe());
            pictures.clear();
            DecodeUnit(*decoder, clip[1], pictures);
            CHECK(pictures.empty());
            CHECK(DecodeUnit(*decoder, clip[6], pictures));
            CHECK(pictures.size() == 1 && QuadrantsAreRight(pictures[0]));
        });

        RunTest("Video decoder: a change of picture size in the middle of the stream", [&]() {
            std::unique_ptr<Marconi::Video::VideoDecoder> decoder = Marconi::Video::CreateFfmpegDecoder();
            const std::vector<Bytes> small = SplitAccessUnits(VideoTestClip::g_small, VideoTestClip::g_smallSize);
            CHECK(decoder != nullptr);
            CHECK(small.size() == 2);
            if (decoder == nullptr || clip.size() != 12 || small.size() != 2)
            {
                CHECK(false);
                return;
            }
            std::vector<Picture> pictures;
            CHECK(DecodeUnit(*decoder, clip[0], pictures));
            CHECK(DecodeUnit(*decoder, clip[1], pictures));
            CHECK(DecodeUnit(*decoder, small[0], pictures)); // 32x32 keyframe with its own SPS
            CHECK(DecodeUnit(*decoder, small[1], pictures));
            CHECK(DecodeUnit(*decoder, clip[6], pictures)); // and back to 64x48
            CHECK(pictures.size() == 5);
            if (pictures.size() == 5)
            {
                CHECK(pictures[1].width == 64 && pictures[1].height == 48);
                CHECK(pictures[2].width == 32 && pictures[2].height == 32);
                CHECK(pictures[3].width == 32 && pictures[3].height == 32);
                CHECK(pictures[4].width == 64 && pictures[4].height == 48);
                CHECK(QuadrantsAreRight(pictures[2]));
                CHECK(QuadrantsAreRight(pictures[3]));
                CHECK(QuadrantsAreRight(pictures[4]));
            }
            CHECK(!decoder->NeedsKeyframe());
        });
    }
} // namespace MarconiTest
