// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's side of a video stream

#include <MarconiCore/Video/VideoReceiver.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>

namespace Marconi
{
    namespace Video
    {
        namespace
        {
            constexpr int64_t kFeedbackIntervalMicros = 500 * 1000;
            constexpr int64_t kKeyframeRetryMicros = 1000 * 1000; // ask again if no keyframe came in this long
            constexpr int64_t kStatsIntervalMicros = 2000 * 1000;
            constexpr int kMaxConsecutiveFailures = 5;

            int64_t SteadyMicros()
            {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            }
        } // namespace

        VideoReceiver::VideoReceiver(VideoDecoderFactory factory, Send send, Clock clock)
            : m_factory(std::move(factory)), m_send(std::move(send)), m_clock(std::move(clock))
        {
            if (!m_factory)
            {
                m_factory = [](Wire::VideoCodec codec) { return CreatePlatformVideoDecoder(codec); };
                m_codecs = PlatformDecodableCodecs();
            }
            if (!m_clock)
            {
                m_clock = SteadyMicros;
            }
        }

        bool VideoReceiver::SendOffer(const VideoPreferences& preferences, uint32_t maxWidth, uint32_t maxHeight)
        {
            if (m_codecs.empty())
            {
                return false; // nothing to decode with: don't offer, the server sends stills
            }
            {
                std::lock_guard<std::mutex> lock(m_preferencesMutex);
                m_preferences = preferences;
            }
            Wire::VideoOffer offer;
            offer.codecs = m_codecs;
            offer.maxWidth = maxWidth;
            offer.maxHeight = maxHeight;
            offer.maxFps = preferences.maxFps;
            if (!m_send(offer.Encode()))
            {
                return false;
            }
            Wire::StreamConfig config;
            config.mode = preferences.mode;
            config.maxBitrateKbps = preferences.maxBitrateKbps;
            config.maxFps = preferences.maxFps;
            return m_send(config.Encode());
        }

        VideoPreferences VideoReceiver::GetPreferences() const
        {
            std::lock_guard<std::mutex> lock(m_preferencesMutex);
            return m_preferences;
        }

        bool VideoReceiver::SetPreferences(const VideoPreferences& preferences)
        {
            {
                std::lock_guard<std::mutex> lock(m_preferencesMutex);
                m_preferences = preferences;
            }
            Wire::StreamConfig config;
            config.mode = preferences.mode;
            config.maxBitrateKbps = preferences.maxBitrateKbps;
            config.maxFps = preferences.maxFps;
            return m_send(config.Encode());
        }

        void VideoReceiver::Fail(Wire::VideoStop::Reason reason)
        {
            m_state = State::Failed;
            m_decoder.reset();
            Wire::VideoStop stop;
            stop.streamId = m_streamId;
            stop.reason = reason;
            m_send(stop.Encode());
            LOG_INFO("Video", "This viewer cannot decode the video stream; the server goes back to still pictures");
        }

        void VideoReceiver::OnStart(const Wire::VideoStart& start)
        {
            m_decoder.reset();
            m_start = start;
            m_streamId = start.streamId;
            m_lastFrameId = 0;
            m_lastGoodFrameId = 0;
            m_needKeyframe = true; // the first frame of a stream is a keyframe; until it arrives nothing decodes
            m_keyframeRequested = false;
            m_consecutiveFailures = 0;
            m_fbReceived = m_fbDropped = 0;
            m_fbDecodeMicros = 0;
            m_lastFeedbackAt = m_clock();
            ++m_stats.streamsStarted;

            m_decoder = m_factory ? m_factory(start.codec) : nullptr;
            if (!m_decoder)
            {
                Fail(Wire::VideoStop::Reason::DecoderFailed);
                return;
            }
            DecoderStreamInfo info;
            info.codec = start.codec;
            info.width = start.width;
            info.height = start.height;
            info.extradata = start.extradata;
            std::string error;
            if (!m_decoder->Configure(info, error))
            {
                LOG_INFO("Video", "Decoder '%s' refused the stream: %s", m_decoder->Name(), error.c_str());
                Fail(Wire::VideoStop::Reason::DecoderFailed);
                return;
            }
            m_state = State::Streaming;
            LOG_INFO("Video", "Stream %u: %s %ux%u at up to %u frames a second, decoding with %s",
                     static_cast<unsigned>(start.streamId), Wire::VideoCodecName(start.codec),
                     static_cast<unsigned>(start.width), static_cast<unsigned>(start.height),
                     static_cast<unsigned>(start.fps), m_decoder->Name());
        }

        void VideoReceiver::OnStop(const Wire::VideoStop& stop)
        {
            if (m_state != State::Idle)
            {
                LOG_INFO("Video", "The server ended the video stream (reason %u); pictures come as stills now",
                         static_cast<unsigned>(stop.reason));
            }
            m_state = State::Idle;
            m_decoder.reset();
        }

        void VideoReceiver::RequestKeyframe(Wire::VideoRequestKeyframe::Reason reason)
        {
            int64_t now = m_clock();
            if (m_keyframeRequested && now - m_keyframeRequestedAt < kKeyframeRetryMicros)
            {
                return; // one is already on its way
            }
            m_keyframeRequested = true;
            m_keyframeRequestedAt = now;
            ++m_stats.keyframesRequested;
            Wire::VideoRequestKeyframe request;
            request.streamId = m_streamId;
            request.reason = reason;
            request.lastGoodFrameId = m_lastGoodFrameId;
            m_send(request.Encode());
        }

        bool VideoReceiver::OnFrame(const Wire::VideoFrame& frame, VideoUpdate& update)
        {
            auto drop = [this](uint64_t count) {
                m_stats.framesDropped += count;
                m_fbDropped += static_cast<uint32_t>(count);
                m_statDropped += count;
            };
            if (m_state != State::Streaming || frame.streamId != m_streamId)
            {
                return false; // no stream, or a late frame of one that ended: nothing to count against this one
            }
            m_stats.bytes += frame.payload.size();
            m_statBytes += frame.payload.size();
            ++m_fbReceived;

            if (!frame.IsKeyframe())
            {
                if (frame.frameId <= m_lastFrameId)
                {
                    drop(1); // a repeat or an old frame
                    return false;
                }
                if (frame.frameId != m_lastFrameId + 1)
                {
                    drop(frame.frameId - m_lastFrameId - 1); // the frames that never arrived
                    m_needKeyframe = true;
                }
                if (m_needKeyframe)
                {
                    drop(1);
                    m_lastFrameId = frame.frameId;
                    RequestKeyframe(Wire::VideoRequestKeyframe::Reason::Gap);
                    return false;
                }
            }
            else if (frame.frameId > m_lastFrameId + 1)
            {
                drop(frame.frameId - m_lastFrameId - 1); // lost before this keyframe, which makes them harmless
            }

            int64_t startedAt = m_clock();
            std::string error;
            DecodeStatus status =
                m_decoder->Decode(frame.payload.data(), frame.payload.size(), frame.IsKeyframe(), m_picture, error);
            int64_t took = std::max<int64_t>(0, m_clock() - startedAt);
            m_lastFrameId = frame.frameId;

            if (status == DecodeStatus::Failed)
            {
                LOG_INFO("Video", "Frame %llu did not decode: %s", static_cast<unsigned long long>(frame.frameId),
                         error.c_str());
                drop(1);
                m_decoder->Reset();
                m_needKeyframe = true;
                m_keyframeRequested = false; // the one asked for earlier is of no use to a decoder just reset
                if (++m_consecutiveFailures >= kMaxConsecutiveFailures)
                {
                    Fail(Wire::VideoStop::Reason::DecoderFailed);
                }
                else
                {
                    RequestKeyframe(Wire::VideoRequestKeyframe::Reason::DecodeError);
                }
                return false;
            }
            m_consecutiveFailures = 0;
            if (frame.IsKeyframe())
            {
                m_needKeyframe = false;
                m_keyframeRequested = false;
            }
            m_fbDecodeMicros += static_cast<uint64_t>(took);
            m_statDecodeMicros += static_cast<uint64_t>(took);
            if (status == DecodeStatus::NeedMoreInput)
            {
                return false;
            }
            m_lastGoodFrameId = frame.frameId;
            ++m_stats.framesDecoded;
            ++m_statFrames;

            // Which parts to copy: those the server listed, clipped to the picture; all of it when none were listed.
            update.picture = &m_picture;
            update.origin = m_start.origin;
            update.rects.clear();
            const int32_t width = m_picture.width;
            const int32_t height = m_picture.height;
            if (frame.applyRects.empty())
            {
                update.rects.push_back({m_start.origin.x, m_start.origin.y, width, height});
            }
            else
            {
                for (const Wire::Rect& rect : frame.applyRects)
                {
                    int32_t left = std::max<int32_t>(rect.x, 0);
                    int32_t top = std::max<int32_t>(rect.y, 0);
                    int32_t right = std::min<int64_t>(static_cast<int64_t>(rect.x) + rect.width, width);
                    int32_t bottom = std::min<int64_t>(static_cast<int64_t>(rect.y) + rect.height, height);
                    if (right > left && bottom > top)
                    {
                        update.rects.push_back(
                            {m_start.origin.x + left, m_start.origin.y + top, right - left, bottom - top});
                    }
                }
            }
            if (m_logStats)
            {
                LogStats(m_clock());
            }
            return !update.rects.empty();
        }

        void VideoReceiver::MaybeSendFeedback()
        {
            if (m_state != State::Streaming)
            {
                return;
            }
            int64_t now = m_clock();
            if (now - m_lastFeedbackAt < kFeedbackIntervalMicros)
            {
                return;
            }
            m_lastFeedbackAt = now;
            Wire::VideoFeedback feedback;
            feedback.streamId = m_streamId;
            feedback.lastFrameId = m_lastFrameId;
            feedback.framesReceived = m_fbReceived;
            feedback.framesDropped = m_fbDropped;
            feedback.decodeMicros = m_fbReceived > 0 ? static_cast<uint32_t>(m_fbDecodeMicros / m_fbReceived) : 0;
            feedback.queueDepth = 0; // frames are decoded as they arrive, so none wait
            const VideoPreferences wishes = GetPreferences();
            feedback.targetBitrateKbps = wishes.maxBitrateKbps;
            feedback.maxFps = wishes.maxFps;
            feedback.quality = wishes.quality;
            m_fbReceived = 0;
            m_fbDropped = 0;
            m_fbDecodeMicros = 0;
            m_send(feedback.Encode());
        }

        void VideoReceiver::LogStats(int64_t now)
        {
            if (m_statsAt == 0)
            {
                m_statsAt = now;
                return;
            }
            int64_t elapsed = now - m_statsAt;
            if (elapsed < kStatsIntervalMicros)
            {
                return;
            }
            double seconds = static_cast<double>(elapsed) / 1e6;
            double decodeMs = m_statFrames > 0 ? static_cast<double>(m_statDecodeMicros) / m_statFrames / 1000.0 : 0.0;
            LOG_INFO("Video", "Viewer: %.1f frames a second, %.0f kbit/s, decode %.2f ms a frame, %llu dropped",
                     m_statFrames / seconds, m_statBytes * 8.0 / 1000.0 / seconds, decodeMs,
                     static_cast<unsigned long long>(m_statDropped));
            m_statsAt = now;
            m_statFrames = 0;
            m_statBytes = 0;
            m_statDecodeMicros = 0;
            m_statDropped = 0;
        }
    } // namespace Video
} // namespace Marconi
