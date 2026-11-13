// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's side of a video stream: follows the stream, decodes, notices lost frames, asks for keyframes and
// reports back. It has no sockets and no clock of its own, so tests drive it directly.

#pragma once

#include <MarconiCore/Video/VideoDecoder.h>
#include <MarconiCore/Wire/WireVideoMessages.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Video
    {
        // What the viewer wants of the stream (sent to the server in StreamConfig and VideoFeedback).
        struct VideoPreferences
        {
            Wire::StreamConfig::Mode mode = Wire::StreamConfig::Mode::Hybrid;
            uint32_t maxBitrateKbps = 0; // 0 = no wish
            uint16_t maxFps = 0;         // 0 = no wish
            uint8_t quality = 0;         // 0 = no wish, 1..100
        };

        // A decoded picture ready to be copied into the framebuffer: the pixels of `picture` inside each of `rects`
        // (rectangles in desktop coordinates; `origin` is where the picture sits on the desktop).
        struct VideoUpdate
        {
            const DecodedPicture* picture = nullptr; // valid until the next call into the receiver
            Wire::Rect origin;
            std::vector<Wire::Rect> rects; // desktop coordinates, already clipped to the picture
        };

        class VideoReceiver
        {
        public:
            using Clock = std::function<int64_t()>;                      // microseconds, any fixed starting point
            using Send = std::function<bool(const Wire::Bytes& framed)>; // a message to the server

            // `factory` makes decoders (null: the platform's). `send` carries the viewer's messages to the server.
            VideoReceiver(VideoDecoderFactory factory, Send send, Clock clock = nullptr);

            // The codecs to offer in a VideoOffer: what `factory` can decode (the platform's when none was given).
            // A factory can't be asked what it supports, so a caller giving its own also gives `codecs`.
            void SetOfferedCodecs(const std::vector<Wire::VideoCodec>& codecs) { m_codecs = codecs; }
            const std::vector<Wire::VideoCodec>& OfferedCodecs() const { return m_codecs; }

            // The messages that start negotiation, after the login: VideoOffer then StreamConfig. False if the send
            // failed.
            bool SendOffer(const VideoPreferences& preferences, uint32_t maxWidth = 0, uint32_t maxHeight = 0);
            // Changes what the viewer wants of the stream; sends StreamConfig (and the next feedback carries the rest).
            bool SetPreferences(const VideoPreferences& preferences);
            VideoPreferences GetPreferences() const;

            // The server's messages. They never throw: a stream that can't be decoded ends with a VideoStop sent to
            // the server and the pictures carry on as Still rectangles.
            void OnStart(const Wire::VideoStart& start);
            // True when `update` holds something to draw.
            bool OnFrame(const Wire::VideoFrame& frame, VideoUpdate& update);
            void OnStop(const Wire::VideoStop& stop);

            // Sends a VideoFeedback if one is due (about every half second while a stream runs). Call after frames.
            void MaybeSendFeedback();

            // True while a stream is running and decoding.
            bool IsStreaming() const { return m_state == State::Streaming; }
            uint32_t StreamId() const { return m_streamId; }
            // The picture's size and place of the running stream.
            const Wire::VideoStart& GetStreamStart() const { return m_start; }

            // Counters since the receiver was made, for tests and the --stats log.
            struct Stats
            {
                uint64_t framesDecoded = 0;
                uint64_t framesDropped = 0;
                uint64_t keyframesRequested = 0;
                uint64_t bytes = 0;
                uint64_t streamsStarted = 0;
            };
            const Stats& GetStats() const { return m_stats; }

            // Writes a line to the log every few seconds: frames a second, bitrate, decode time, drops.
            void SetStatsLogging(bool on) { m_logStats = on; }

        private:
            enum class State
            {
                Idle,      // no stream
                Streaming, // decoding
                Failed     // the decoder could not be used; stills only until the server starts another stream
            };
            void RequestKeyframe(Wire::VideoRequestKeyframe::Reason reason);
            void Fail(Wire::VideoStop::Reason reason);
            void LogStats(int64_t now);

            VideoDecoderFactory m_factory;
            Send m_send;
            Clock m_clock;
            std::vector<Wire::VideoCodec> m_codecs;
            mutable std::mutex m_preferencesMutex; // preferences are set from the UI thread, read on the worker
            VideoPreferences m_preferences;

            State m_state = State::Idle;
            std::unique_ptr<VideoDecoder> m_decoder;
            Wire::VideoStart m_start;
            uint32_t m_streamId = 0;
            uint64_t m_lastFrameId = 0;     // the newest frame accepted in this stream
            uint64_t m_lastGoodFrameId = 0; // the newest that decoded
            bool m_needKeyframe = false;    // dropping delta frames until a keyframe arrives
            int64_t m_keyframeRequestedAt = 0;
            bool m_keyframeRequested = false;
            int m_consecutiveFailures = 0;
            DecodedPicture m_picture;

            // Since the last feedback.
            uint32_t m_fbReceived = 0;
            uint32_t m_fbDropped = 0;
            uint64_t m_fbDecodeMicros = 0;
            int64_t m_lastFeedbackAt = 0;
            // Since the last stats line.
            bool m_logStats = false;
            int64_t m_statsAt = 0;
            uint64_t m_statFrames = 0;
            uint64_t m_statBytes = 0;
            uint64_t m_statDecodeMicros = 0;
            uint64_t m_statDropped = 0;
            Stats m_stats;
        };
    } // namespace Video
} // namespace Marconi
