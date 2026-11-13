// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's video: negotiation, when to send video and when stills, pacing, rate control, keyframes, fallback
//
// The Session owns one of these when the viewer was granted Capability::Video. The viewer's video messages are
// handed to the On... calls (any thread; they only record); the frame thread calls Process for every grab, and
// everything that is sent goes through the `send` function given at construction. Time comes from a clock
// function, so tests drive it with a fake one.
//
// How video and stills share the screen (the decision is recorded in SERVER_PLAN.md, "Phase 4"):
//  - Hybrid: a change covering more than 1/32 of the picture is encoded as video. A smaller change (typing, a
//    menu, the pointer's blink) is sent as lossless stills at once, because video would blur text for a moment for
//    no gain. Areas sent as video are resent as stills once nothing has changed for `stillDelayMs`.
//  - Video: everything is video, never refreshed.
//  - StillOnly: no video; the Session sends stills as before.
// Each VideoFrame names the rectangles the viewer should copy from its picture, so stills are not painted over.

#pragma once

#include "ScreenSource.h"
#include "VideoEncoder.h"
#include "VideoRateControl.h"

#include <MarconiCore/Wire/WireVideoMessages.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        struct VideoSessionOptions
        {
            uint32_t stillDelayMs = 300;       // how long a part must be still before it is resent as a lossless still
            uint32_t smallChangeDivisor = 32;  // a change under 1/this of the picture goes as stills (Hybrid)
            uint32_t defaultFps = 60;
            uint32_t serverCeilingKbps = 40000;
            uint32_t keyframeMinIntervalMs = 100; // keyframe requests closer together than this wait
            uint32_t keyframeIntervalFrames = 0;  // 0 = four seconds' worth of frames
            int maxEncodeFailures = 3;            // in a row, then video stops and stills take over
            bool logStats = false;                // the --stats output
            uint32_t statsIntervalMs = 2000;
        };

        struct VideoStats
        {
            uint64_t framesSent = 0;
            uint64_t keyframesSent = 0;
            uint64_t videoBytes = 0;
            uint64_t stillMessages = 0; // Still rectangles sent by the video session (small changes and refreshes)
            uint64_t streamsStarted = 0;
            uint64_t keyframeRequestsServed = 0;
            double captureToSendTotalMs = 0; // over every frame sent
            double captureToSendMaxMs = 0;
            double encodeTotalMs = 0;
        };

        class VideoSession
        {
        public:
            using Clock = std::function<int64_t()>;                      // microseconds, any fixed starting point
            using Send = std::function<bool(const Wire::Bytes& framed)>; // false if the connection failed

            // `available` is what `factory` can encode, in the server's order of preference.
            VideoSession(VideoEncoderFactory factory, std::vector<Wire::VideoCodec> available, Send send,
                         Clock clock = nullptr, const VideoSessionOptions& options = VideoSessionOptions());
            ~VideoSession();

            VideoSession(const VideoSession&) = delete;
            VideoSession& operator=(const VideoSession&) = delete;

            // The viewer's messages (input thread). They record; the frame thread acts on them.
            void OnOffer(const Wire::VideoOffer& offer);
            void OnStreamConfig(const Wire::StreamConfig& config);
            void OnKeyframeRequest(const Wire::VideoRequestKeyframe& request);
            void OnFeedback(const Wire::VideoFeedback& feedback);
            void OnViewerStop(const Wire::VideoStop& stop);

            // True when the frame thread should call Process for this grab, whether or not anything changed: video
            // is running or about to start. When false the Session sends stills as it always has.
            bool WantsFrames() const;

            enum class Result
            {
                Handled,   // the picture was dealt with (video, stills, or nothing to send)
                UseStills, // video is not (or no longer) running: send this capture as stills, whole if `full`
                SendFailed // the connection failed
            };
            // One grab. `area` is the part of the capture being streamed (the selected display); `full` says the whole
            // area must reach the viewer (the first picture, a new display); `stillFrameId` is the Session's counter
            // for FrameBegin ids, which the stills sent here advance.
            Result Process(const Capture& capture, const Wire::Rect& area, bool full, uint64_t& stillFrameId);

            VideoStats GetStats() const;
            uint32_t CurrentBitrateKbps() const { return m_rate ? m_rate->Current() : 0; }
            bool IsStreaming() const { return m_streaming.load(); }

        private:
            Result Fallback(Wire::VideoStop::Reason reason, bool sendStop);
            bool StartStream(const Wire::Rect& area, int64_t now, Wire::VideoStop::Reason& failure);
            void Teardown();
            bool SendStills(const Capture& capture, const std::vector<Wire::Rect>& rects, uint64_t& stillFrameId);
            void AddDirty(std::vector<Wire::Rect>& into, const Wire::Rect& rect) const;
            void LogStats(int64_t now);

            VideoEncoderFactory m_factory;
            std::vector<Wire::VideoCodec> m_available;
            Send m_send;
            Clock m_clock;
            VideoSessionOptions m_options;

            // Set by the input thread, read by the frame thread.
            mutable std::mutex m_mutex;
            bool m_haveOffer = false;
            Wire::VideoOffer m_offer;
            bool m_haveConfig = false;
            Wire::StreamConfig m_config;
            bool m_keyframeWanted = false;
            std::vector<Wire::VideoFeedback> m_feedback;
            bool m_viewerStopped = false;
            bool m_failed = false;  // the viewer's decoder or the server's encoder failed: stills until a new offer
            bool m_refused = false; // no codec in common: stills until a new offer
            std::atomic<bool> m_streaming{false};
            std::atomic<uint32_t> m_activeStreamId{0};

            // Frame thread only.
            std::unique_ptr<VideoEncoder> m_encoder;
            std::unique_ptr<VideoRateControl> m_rate;
            Wire::VideoCodec m_codec = Wire::VideoCodec::None;
            uint32_t m_streamCounter = 0;
            Wire::Rect m_area;           // the area the running stream shows
            uint64_t m_frameId = 0;      // within the stream
            uint32_t m_fps = 60;
            int64_t m_lastEncodeAt = -1;
            int64_t m_lastKeyframeAt = -1;
            int64_t m_lastChangeAt = 0;
            int m_encodeFailures = 0;
            uint32_t m_appliedKbps = 0; // the bitrate last given to the encoder
            double m_windowWorstMs = 0;
            std::vector<Wire::Rect> m_pendingVideo; // changed since the last video frame, in picture coordinates
            std::vector<Wire::Rect> m_pendingStill; // sent as video, not yet as a lossless still
            VideoStats m_stats;
            int64_t m_statsWindowStart = 0;
            VideoStats m_statsAtWindowStart;
        };
    } // namespace Server
} // namespace Marconi
