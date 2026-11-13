// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A deterministic video codec that exists only for the tests: it needs no libraries and no hardware, so the whole
// video pipeline (negotiation, keyframes, loss, size changes, fallback, pacing) can be tested on any machine.
//
// It is lossy on purpose: the low four bits of every colour channel are dropped, so a picture that went through
// video differs from the screen and a lossless still that follows it visibly fixes that (tests check both). A
// delta frame holds the XOR of the picture with the one before it, zlib-compressed. Every frame carries a
// sequence number, and the decoder refuses a delta frame that does not follow the last one it decoded, so a
// receiver that forgets to ask for a keyframe after a loss fails the test instead of passing by luck.

#pragma once

#include <MarconiCore/Video/VideoDecoder.h>
#include <MarconiServerCore/VideoEncoder.h>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace MarconiServerTest
{
    // What the codec did, shared with the test so it can look after the session (and so the encoder) is gone.
    struct CodecProbe
    {
        std::mutex mutex;
        int encodersCreated = 0;
        int encodersAlive = 0;
        int decodersCreated = 0;
        int decodersAlive = 0;
        std::vector<std::pair<uint32_t, uint32_t>> configs; // the size each Configure was given
        std::vector<uint32_t> configBitrates;               // the bitrate each Configure was given
        std::vector<uint32_t> bitrates;                     // every SetBitrate
        int keyframesForced = 0;
        int picturesEncoded = 0;
        int keyframesEncoded = 0;
        int picturesDecoded = 0;
        int decodeFailures = 0;
        bool failConfigure = false;      // the encoder refuses to start
        int failEncodeAfter = -1;        // the encoder fails every call after this many pictures (-1 = never)
        bool decoderRefusesConfigure = false;
    };

    // A pixel as the test codec reproduces it.
    inline uint32_t TestCodecQuantize(uint32_t pixel)
    {
        return pixel & 0x00F0F0F0u;
    }

    Marconi::Server::VideoEncoderFactory MakeTestEncoderFactory(std::shared_ptr<CodecProbe> probe);
    Marconi::Video::VideoDecoderFactory MakeTestDecoderFactory(std::shared_ptr<CodecProbe> probe);
} // namespace MarconiServerTest
