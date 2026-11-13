// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol's video messages: agreeing a codec, the encoded pictures, and the viewer's feedback
//
// How video and the lossless Still rectangles live together is decided in SERVER_PLAN.md ("Phase 4: video"):
// a viewer that agreed a video stream gets the moving parts of the screen as encoded pictures and, once a part
// has been still for a moment, a lossless Still for it. Every VideoFrame says which rectangles of its picture the
// viewer should copy into its framebuffer (`applyRects`), so a Still that already made some text sharp is not
// painted over by a blurry video picture of the same text.
//
// Flow, all on the control channel:
//   viewer  Hello with Capability::Video   server Welcome grants it only if it has an encoder
//   viewer  VideoOffer (codecs it can decode, in its order of preference)
//   viewer  StreamConfig (mode Video or Hybrid; maximum bitrate and frame rate)
//   server  VideoStart (the chosen codec, size, frame rate, bitrate, codec set-up bytes)   -- or VideoStop if none
//   server  VideoFrame ...   (the first of every stream is a keyframe)
//   viewer  VideoFeedback every half second or so; VideoRequestKeyframe when its decoder lost its place
//   server  VideoStart again (a new stream id) when the picture's size changes; VideoStop to end
// Anything unexpected falls back to Still rectangles, which every viewer understands.

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        // The codecs a stream can use. The numbers are on the wire and never change; a new codec takes the next free one.
        enum class VideoCodec : uint8_t
        {
            None = 0,
            H264 = 1,  // Annex B byte stream, no B-frames, parameter sets repeated on every keyframe
            Hevc = 2,  // not implemented yet
            Av1 = 3,   // not implemented yet
            Test = 250 // a codec that exists only in the tests; a real build never offers or chooses it
        };

        // Limits a decoder of these messages enforces, so a hostile peer can't make it allocate wildly.
        constexpr uint32_t kMaxVideoDimension = 16384;
        constexpr size_t kMaxVideoCodecs = 16;
        constexpr size_t kMaxVideoApplyRects = 256;
        constexpr size_t kMaxVideoExtradata = 64 * 1024;

        // The viewer's list of what it can decode, best first, with the biggest picture and fastest rate it can take.
        struct VideoOffer
        {
            std::vector<VideoCodec> codecs;
            uint32_t maxWidth = 0; // 0 = no limit the viewer knows of
            uint32_t maxHeight = 0;
            uint16_t maxFps = 0; // 0 = no limit

            Bytes Encode() const;
            static VideoOffer Decode(Reader& reader);
        };

        // A video stream begins. `streamId` is new for every stream (a size change starts a new one); frames carry
        // it so a late frame of an old stream is dropped.
        struct VideoStart
        {
            uint32_t streamId = 0;
            VideoCodec codec = VideoCodec::None;
            uint32_t width = 0; // the picture's size in pixels
            uint32_t height = 0;
            uint16_t fps = 0;         // the most frames a second the server will send
            uint32_t bitrateKbps = 0; // where the rate starts; it moves with the viewer's feedback
            // The area of the desktop the picture shows (desktop coordinates): where the picture goes in the viewer.
            Rect origin;
            Bytes extradata; // codec set-up bytes (H.264: SPS and PPS), possibly empty

            Bytes Encode() const;
            static VideoStart Decode(Reader& reader);
        };

        struct VideoFrame
        {
            static constexpr uint8_t kKeyframe = 1; // flag: decodes without any earlier frame
            uint32_t streamId = 0;
            uint64_t frameId = 0; // 1, 2, 3... within the stream; a gap means a frame was lost
            uint8_t flags = 0;
            uint64_t captureMicros = 0; // when the screen was grabbed, on the server's clock (any fixed starting point)
            uint32_t encodeMicros = 0;  // how long the encoder took
            // The rectangles of the decoded picture to copy into the viewer's framebuffer, relative to the picture.
            // Empty means all of it.
            std::vector<Rect> applyRects;
            Bytes payload;

            bool IsKeyframe() const { return (flags & kKeyframe) != 0; }
            Bytes Encode() const;
            static VideoFrame Decode(Reader& reader);
        };

        struct VideoRequestKeyframe
        {
            enum class Reason : uint8_t
            {
                Gap = 0,         // a frame number was skipped
                DecodeError = 1, // the decoder refused a frame
                Other = 2
            };
            uint32_t streamId = 0;
            Reason reason = Reason::Gap;
            uint64_t lastGoodFrameId = 0; // the last frame that decoded cleanly (0 = none)

            Bytes Encode() const;
            static VideoRequestKeyframe Decode(Reader& reader);
        };

        // The viewer's report, and its wishes for the stream.
        struct VideoFeedback
        {
            uint32_t streamId = 0;
            uint64_t lastFrameId = 0;       // the newest frame that arrived
            uint32_t framesReceived = 0;    // since the last report
            uint32_t framesDropped = 0;     // frames the viewer skipped or could not decode since the last report
            uint32_t decodeMicros = 0;      // average decode time per frame since the last report
            uint8_t queueDepth = 0;         // frames waiting to be decoded or shown when the report was made
            uint32_t targetBitrateKbps = 0; // the most the viewer wants (0 = no wish)
            uint16_t maxFps = 0;            // the most frames a second the viewer wants (0 = no wish)
            uint8_t quality = 0;            // 0 = no wish, 1..100 = how much it prefers sharpness over speed

            Bytes Encode() const;
            static VideoFeedback Decode(Reader& reader);
        };

        // Ends a stream. From the server it says why the pictures are Still rectangles again; from the viewer it
        // says it cannot decode the stream, so the server stops it.
        struct VideoStop
        {
            enum class Reason : uint8_t
            {
                ClientRequest = 0, // the viewer asked for still only (or cannot decode)
                NoCommonCodec = 1, // no codec both ends have
                EncoderFailed = 2, // the server's encoder failed
                DecoderFailed = 3, // the viewer's decoder failed
                Switched = 4,      // the server changed mode (stills only now)
                Unavailable = 5    // this server has no video encoder
            };
            uint32_t streamId = 0; // 0 when no stream had started
            Reason reason = Reason::ClientRequest;

            Bytes Encode() const;
            static VideoStop Decode(Reader& reader);
        };

        // A short name for logs.
        const char* VideoCodecName(VideoCodec codec);
    } // namespace Wire
} // namespace Marconi
