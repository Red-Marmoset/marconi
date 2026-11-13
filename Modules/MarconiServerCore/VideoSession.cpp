// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One viewer's video: negotiation, when to send video and when stills, pacing, rate control, keyframes, fallback

#include "VideoSession.h"

#include <MarconiCore/Wire/WireCompress.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr size_t kMaxTrackedRects = 32; // more than this many pending rectangles collapse to their box

            int64_t SteadyMicros()
            {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            }

            bool Overlaps(const Wire::Rect& a, const Wire::Rect& b)
            {
                // Touching edges count, so neighbours join into one rectangle.
                return a.x <= b.x + b.width && b.x <= a.x + a.width && a.y <= b.y + b.height && b.y <= a.y + a.height;
            }

            Wire::Rect Union(const Wire::Rect& a, const Wire::Rect& b)
            {
                int32_t left = std::min(a.x, b.x);
                int32_t top = std::min(a.y, b.y);
                int32_t right = std::max(a.x + a.width, b.x + b.width);
                int32_t bottom = std::max(a.y + a.height, b.y + b.height);
                return {left, top, right - left, bottom - top};
            }

            Wire::Rect Intersect(const Wire::Rect& a, const Wire::Rect& b)
            {
                int32_t left = std::max(a.x, b.x);
                int32_t top = std::max(a.y, b.y);
                int32_t right = std::min(a.x + a.width, b.x + b.width);
                int32_t bottom = std::min(a.y + a.height, b.y + b.height);
                if (right <= left || bottom <= top)
                {
                    return {0, 0, 0, 0};
                }
                return {left, top, right - left, bottom - top};
            }

            uint64_t AreaOf(const std::vector<Wire::Rect>& rects)
            {
                uint64_t total = 0;
                for (const Wire::Rect& rect : rects)
                {
                    total += static_cast<uint64_t>(rect.width) * static_cast<uint64_t>(rect.height);
                }
                return total;
            }

            bool RectInside(const Wire::Rect& rect, int32_t width, int32_t height)
            {
                return rect.width > 0 && rect.height > 0 && rect.x >= 0 && rect.y >= 0 &&
                       rect.x + rect.width <= width && rect.y + rect.height <= height;
            }

            // A still's pixels as little-endian B, G, R, 0 bytes, row by row (the same packing the Session uses).
            Wire::Bytes PackRect(const Capture& capture, const Wire::Rect& rect)
            {
                Wire::Bytes data;
                data.reserve(static_cast<size_t>(rect.width) * rect.height * 4);
                for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
                {
                    const uint32_t* row = capture.Row(y);
                    for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
                    {
                        uint32_t pixel = row[x];
                        data.push_back(static_cast<uint8_t>(pixel));
                        data.push_back(static_cast<uint8_t>(pixel >> 8));
                        data.push_back(static_cast<uint8_t>(pixel >> 16));
                        data.push_back(0);
                    }
                }
                return data;
            }

            uint32_t PickFps(uint32_t a, uint32_t b)
            {
                if (a == 0)
                {
                    return b;
                }
                return b == 0 ? a : std::min(a, b);
            }
        } // namespace

        VideoSession::VideoSession(VideoEncoderFactory factory, std::vector<Wire::VideoCodec> available, Send send,
                                   Clock clock, const VideoSessionOptions& options)
            : m_factory(std::move(factory)), m_available(std::move(available)), m_send(std::move(send)),
              m_clock(std::move(clock)), m_options(options)
        {
            if (!m_clock)
            {
                m_clock = SteadyMicros;
            }
        }

        VideoSession::~VideoSession()
        {
            Teardown();
        }

        // -- The viewer's messages ------------------------------------------------------------------------------

        void VideoSession::OnOffer(const Wire::VideoOffer& offer)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_offer = offer;
            m_haveOffer = true;
            m_failed = false; // a new offer is a fresh start
            m_refused = false;
        }

        void VideoSession::OnStreamConfig(const Wire::StreamConfig& config)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config = config;
            m_haveConfig = true;
        }

        void VideoSession::OnKeyframeRequest(const Wire::VideoRequestKeyframe& request)
        {
            if (request.streamId != m_activeStreamId.load())
            {
                return; // about a stream that is not the running one
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_keyframeWanted = true;
        }

        void VideoSession::OnFeedback(const Wire::VideoFeedback& feedback)
        {
            if (feedback.streamId != m_activeStreamId.load())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_feedback.size() < 64)
            {
                m_feedback.push_back(feedback);
            }
        }

        void VideoSession::OnViewerStop(const Wire::VideoStop& stop)
        {
            if (stop.streamId != 0 && stop.streamId != m_activeStreamId.load())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_viewerStopped = true;
        }

        bool VideoSession::WantsFrames() const
        {
            if (m_streaming.load())
            {
                return true;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_haveOffer && m_haveConfig && !m_failed && !m_refused &&
                   m_config.mode != Wire::StreamConfig::Mode::StillOnly;
        }

        VideoStats VideoSession::GetStats() const
        {
            return m_stats;
        }

        // -- Frame thread ---------------------------------------------------------------------------------------

        void VideoSession::Teardown()
        {
            if (m_encoder)
            {
                std::vector<EncodedPicture> held;
                m_encoder->Flush(held);
                m_encoder.reset();
            }
            m_rate.reset();
            m_streaming = false;
            m_activeStreamId = 0;
            m_pendingVideo.clear();
            m_pendingStill.clear();
        }

        VideoSession::Result VideoSession::Fallback(Wire::VideoStop::Reason reason, bool sendStop)
        {
            const uint32_t streamId = m_activeStreamId.load();
            Teardown();
            if (sendStop)
            {
                Wire::VideoStop stop;
                stop.streamId = streamId;
                stop.reason = reason;
                if (!m_send(stop.Encode()))
                {
                    return Result::SendFailed;
                }
            }
            return Result::UseStills;
        }

        void VideoSession::AddDirty(std::vector<Wire::Rect>& into, const Wire::Rect& rect) const
        {
            Wire::Rect merged = rect;
            // Join with every rectangle it touches (repeating, since a join can reach one it missed before).
            bool again = true;
            while (again)
            {
                again = false;
                for (size_t i = 0; i < into.size(); ++i)
                {
                    if (Overlaps(into[i], merged))
                    {
                        merged = Union(into[i], merged);
                        into.erase(into.begin() + static_cast<ptrdiff_t>(i));
                        again = true;
                        break;
                    }
                }
            }
            into.push_back(merged);
            if (into.size() > kMaxTrackedRects)
            {
                Wire::Rect box = into.front();
                for (const Wire::Rect& each : into)
                {
                    box = Union(box, each);
                }
                into.assign(1, box);
            }
        }

        bool VideoSession::StartStream(const Wire::Rect& area, int64_t now, Wire::VideoStop::Reason& failure)
        {
            Wire::VideoOffer offer;
            Wire::StreamConfig config;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                offer = m_offer;
                config = m_config;
            }
            Teardown();

            // The first codec in the viewer's order that this server can also encode.
            Wire::VideoCodec chosen = Wire::VideoCodec::None;
            for (Wire::VideoCodec wanted : offer.codecs)
            {
                if (std::find(m_available.begin(), m_available.end(), wanted) != m_available.end())
                {
                    chosen = wanted;
                    break;
                }
            }
            if (chosen == Wire::VideoCodec::None)
            {
                failure = Wire::VideoStop::Reason::NoCommonCodec;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_refused = true;
                LOG_INFO("Video", "No video codec in common with the viewer; sending still pictures");
                return false;
            }
            if ((offer.maxWidth != 0 && static_cast<uint32_t>(area.width) > offer.maxWidth) ||
                (offer.maxHeight != 0 && static_cast<uint32_t>(area.height) > offer.maxHeight))
            {
                // The viewer's decoder can't take a picture this big. (Scaling down is future work.)
                failure = Wire::VideoStop::Reason::NoCommonCodec;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_refused = true;
                LOG_INFO("Video", "The picture (%dx%d) is bigger than the viewer's decoder takes; sending stills",
                         area.width, area.height);
                return false;
            }

            std::unique_ptr<VideoEncoder> encoder = m_factory ? m_factory(chosen) : nullptr;
            if (!encoder)
            {
                failure = Wire::VideoStop::Reason::Unavailable;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_failed = true;
                return false;
            }
            uint32_t fps = PickFps(PickFps(offer.maxFps, config.maxFps), m_options.defaultFps);
            if (fps == 0)
            {
                fps = 60;
            }
            VideoEncoderConfig encoderConfig;
            encoderConfig.codec = chosen;
            encoderConfig.width = static_cast<uint32_t>(area.width);
            encoderConfig.height = static_cast<uint32_t>(area.height);
            encoderConfig.fps = fps;
            // Where the rate starts: about 0.07 bits a pixel a frame, inside 1 to 40 Mbit/s, then the viewer's limit.
            uint64_t guess = static_cast<uint64_t>(area.width) * static_cast<uint64_t>(area.height) * fps * 7 / 100 / 1000;
            guess = std::min<uint64_t>(std::max<uint64_t>(guess, 1000), m_options.serverCeilingKbps);
            auto rate = std::make_unique<VideoRateControl>(m_options.serverCeilingKbps, static_cast<uint32_t>(guess), fps);
            rate->SetConfigMaximum(config.maxBitrateKbps);
            encoderConfig.bitrateKbps = rate->Current();
            encoderConfig.keyframeInterval =
                m_options.keyframeIntervalFrames != 0 ? m_options.keyframeIntervalFrames : fps * 4;
            std::string error;
            if (!encoder->Configure(encoderConfig, error))
            {
                LOG_INFO("Video", "Encoder '%s' could not start: %s", encoder->Name(), error.c_str());
                failure = Wire::VideoStop::Reason::EncoderFailed;
                std::lock_guard<std::mutex> lock(m_mutex);
                m_failed = true;
                return false;
            }

            m_encoder = std::move(encoder);
            m_rate = std::move(rate);
            m_codec = chosen;
            m_fps = fps;
            m_area = area;
            m_frameId = 0;
            m_encodeFailures = 0;
            m_lastEncodeAt = -1;
            m_lastKeyframeAt = -1;
            m_lastChangeAt = now;
            m_pendingStill.clear();
            m_pendingVideo.assign(1, Wire::Rect{0, 0, area.width, area.height}); // the first picture is the whole area
            ++m_streamCounter;
            ++m_stats.streamsStarted;
            m_activeStreamId = m_streamCounter;
            m_streaming = true;
            m_encoder->ForceKeyframe();

            Wire::VideoStart start;
            start.streamId = m_streamCounter;
            start.codec = chosen;
            start.width = static_cast<uint32_t>(area.width);
            start.height = static_cast<uint32_t>(area.height);
            start.fps = static_cast<uint16_t>(std::min<uint32_t>(fps, 65535));
            start.bitrateKbps = m_rate->Current();
            start.origin = area;
            start.extradata = m_encoder->GetExtradata();
            LOG_INFO("Video", "Stream %u: %s %dx%d at up to %u frames a second, %u kbit/s, encoding with %s",
                     static_cast<unsigned>(start.streamId), Wire::VideoCodecName(chosen), area.width, area.height,
                     static_cast<unsigned>(fps), static_cast<unsigned>(start.bitrateKbps), m_encoder->Name());
            if (!m_send(start.Encode()))
            {
                failure = Wire::VideoStop::Reason::EncoderFailed; // unused: the caller sees the failed send below
                m_streaming = false;
                return false;
            }
            return true;
        }

        bool VideoSession::SendStills(const Capture& capture, const std::vector<Wire::Rect>& rects,
                                      uint64_t& stillFrameId)
        {
            std::vector<Wire::Rect> usable;
            for (const Wire::Rect& rect : rects)
            {
                if (RectInside(rect, capture.width, capture.height))
                {
                    usable.push_back(rect);
                }
            }
            if (usable.empty())
            {
                return true;
            }
            Wire::FrameBegin begin;
            begin.frameId = ++stillFrameId;
            begin.displayId = 0;
            if (!m_send(begin.Encode()))
            {
                return false;
            }
            for (const Wire::Rect& rect : usable)
            {
                Wire::Still still;
                still.rect = rect;
                Wire::Bytes raw = PackRect(capture, rect);
                Wire::Bytes packed = Wire::ZlibCompress(raw);
                if (!packed.empty() && packed.size() < raw.size())
                {
                    still.compression = Wire::Still::Compression::Zlib;
                    still.data = std::move(packed);
                }
                else
                {
                    still.compression = Wire::Still::Compression::None;
                    still.data = std::move(raw);
                }
                if (!m_send(still.Encode()))
                {
                    return false;
                }
                ++m_stats.stillMessages;
            }
            Wire::FrameEnd end;
            end.frameId = begin.frameId;
            return m_send(end.Encode());
        }

        VideoSession::Result VideoSession::Process(const Capture& capture, const Wire::Rect& area, bool full,
                                                   uint64_t& stillFrameId)
        {
            const int64_t now = m_clock();

            // Take in what the viewer said since the last grab.
            Wire::StreamConfig config;
            bool haveConfig = false;
            bool keyframeWanted = false;
            bool viewerStopped = false;
            std::vector<Wire::VideoFeedback> feedback;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                config = m_config;
                haveConfig = m_haveConfig;
                keyframeWanted = m_keyframeWanted;
                viewerStopped = m_viewerStopped;
                m_viewerStopped = false;
                feedback.swap(m_feedback);
                if (viewerStopped)
                {
                    m_failed = true; // its decoder can't take the stream: stills until it offers again
                }
            }
            if (viewerStopped)
            {
                LOG_INFO("Video", "The viewer cannot decode the video; sending still pictures");
                Fallback(Wire::VideoStop::Reason::ClientRequest, false);
                return Result::UseStills;
            }
            const Wire::StreamConfig::Mode mode = haveConfig ? config.mode : Wire::StreamConfig::Mode::StillOnly;
            if (mode == Wire::StreamConfig::Mode::StillOnly)
            {
                bool wasStreaming = m_streaming.load();
                Result result = Fallback(Wire::VideoStop::Reason::Switched, wasStreaming);
                return result == Result::SendFailed ? result : Result::UseStills;
            }

            // Start, or restart when the picture's size or place changed (a new display, a resolution change).
            const bool sameArea = m_area.x == area.x && m_area.y == area.y && m_area.width == area.width &&
                                  m_area.height == area.height;
            if (!m_streaming.load() || !sameArea)
            {
                Wire::VideoStop::Reason failure = Wire::VideoStop::Reason::EncoderFailed;
                if (!StartStream(area, now, failure))
                {
                    Teardown();
                    Wire::VideoStop stop;
                    stop.streamId = 0;
                    stop.reason = failure;
                    if (!m_send(stop.Encode()))
                    {
                        return Result::SendFailed;
                    }
                    return Result::UseStills;
                }
                full = true;
            }

            // Rate control: the viewer's reports, and the limits in its StreamConfig.
            if (m_rate)
            {
                m_rate->SetConfigMaximum(config.maxBitrateKbps);
                bool changed = false;
                for (const Wire::VideoFeedback& report : feedback)
                {
                    changed = m_rate->OnFeedback(report) || changed;
                    uint32_t wanted = PickFps(PickFps(config.maxFps, report.maxFps), m_options.defaultFps);
                    if (wanted != 0 && wanted != m_fps)
                    {
                        m_fps = wanted;
                        m_rate->SetFps(wanted);
                    }
                }
                if (changed || m_appliedKbps != m_rate->Current())
                {
                    m_appliedKbps = m_rate->Current();
                    m_encoder->SetBitrate(m_appliedKbps);
                }
            }

            // What changed since the last video frame.
            bool changedNow = false;
            if (full)
            {
                m_pendingVideo.assign(1, Wire::Rect{0, 0, area.width, area.height});
                m_pendingStill.clear();
                changedNow = true;
            }
            else
            {
                for (const Wire::Rect& dirty : capture.dirty)
                {
                    Wire::Rect clipped = Intersect(dirty, area);
                    if (clipped.width > 0 && clipped.height > 0)
                    {
                        AddDirty(m_pendingVideo, {clipped.x - area.x, clipped.y - area.y, clipped.width, clipped.height});
                        changedNow = true;
                    }
                }
            }
            if (changedNow)
            {
                m_lastChangeAt = now;
            }
            if (keyframeWanted)
            {
                // Throttled: a request closer than the minimum to the last keyframe waits for the next grab.
                if (m_lastKeyframeAt >= 0 &&
                    now - m_lastKeyframeAt < static_cast<int64_t>(m_options.keyframeMinIntervalMs) * 1000)
                {
                    keyframeWanted = false;
                }
                else
                {
                    m_encoder->ForceKeyframe();
                }
            }

            const uint64_t pictureArea = static_cast<uint64_t>(area.width) * static_cast<uint64_t>(area.height);
            const bool hybrid = mode == Wire::StreamConfig::Mode::Hybrid;
            const int64_t frameInterval = 1000000 / std::max<uint32_t>(m_fps, 1);
            const bool due = m_lastEncodeAt < 0 || now - m_lastEncodeAt >= frameInterval;

            if (!m_pendingVideo.empty() && !keyframeWanted && hybrid && m_frameId > 0 && !full &&
                AreaOf(m_pendingVideo) * m_options.smallChangeDivisor <= pictureArea)
            {
                // A small change: lossless straight away, no video for it.
                std::vector<Wire::Rect> absolute;
                for (const Wire::Rect& rect : m_pendingVideo)
                {
                    absolute.push_back({rect.x + area.x, rect.y + area.y, rect.width, rect.height});
                }
                m_pendingVideo.clear();
                if (!SendStills(capture, absolute, stillFrameId))
                {
                    return Result::SendFailed;
                }
            }
            else if (!m_pendingVideo.empty() || keyframeWanted)
            {
                if (due || keyframeWanted)
                {
                    EncodedPicture picture;
                    std::string error;
                    const int64_t encodeStart = m_clock();
                    EncodeStatus status = m_encoder->Encode(capture, area, picture, error);
                    const int64_t encodeEnd = m_clock();
                    if (status == EncodeStatus::Failed)
                    {
                        LOG_INFO("Video", "The encoder failed: %s", error.c_str());
                        if (++m_encodeFailures >= m_options.maxEncodeFailures)
                        {
                            Result result = Fallback(Wire::VideoStop::Reason::EncoderFailed, true);
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_failed = true;
                            return result;
                        }
                        return Result::Handled; // try again on the next grab
                    }
                    if (status == EncodeStatus::Picture)
                    {
                        m_encodeFailures = 0;
                        Wire::VideoFrame frame;
                        frame.streamId = m_streamCounter;
                        frame.frameId = ++m_frameId;
                        frame.flags = picture.keyframe ? Wire::VideoFrame::kKeyframe : 0;
                        frame.captureMicros = static_cast<uint64_t>(now);
                        if (picture.encodeMicros == 0)
                        {
                            picture.encodeMicros = static_cast<uint32_t>(std::max<int64_t>(0, encodeEnd - encodeStart));
                        }
                        frame.encodeMicros = picture.encodeMicros;
                        if (m_pendingVideo.empty())
                        {
                            // A keyframe for a viewer that lost its place and nothing new to show: an empty rectangle
                            // means "copy nothing", so the picture on screen is not touched.
                            frame.applyRects.push_back({0, 0, 0, 0});
                        }
                        else
                        {
                            frame.applyRects = m_pendingVideo;
                        }
                        frame.payload = std::move(picture.data);
                        m_stats.videoBytes += frame.payload.size();
                        Wire::Bytes framed = frame.Encode();
                        if (!m_send(framed))
                        {
                            return Result::SendFailed;
                        }
                        const int64_t sentAt = m_clock();
                        ++m_stats.framesSent;
                        double toSend = static_cast<double>(std::max<int64_t>(0, sentAt - now)) / 1000.0;
                        m_stats.captureToSendTotalMs += toSend;
                        m_stats.captureToSendMaxMs = std::max(m_stats.captureToSendMaxMs, toSend);
                        m_stats.encodeTotalMs += static_cast<double>(picture.encodeMicros) / 1000.0;
                        m_windowWorstMs = std::max(m_windowWorstMs, toSend);
                        if (picture.keyframe)
                        {
                            ++m_stats.keyframesSent;
                            m_lastKeyframeAt = now;
                            if (keyframeWanted)
                            {
                                ++m_stats.keyframeRequestsServed;
                                std::lock_guard<std::mutex> lock(m_mutex);
                                m_keyframeWanted = false;
                            }
                        }
                        m_lastEncodeAt = now;
                        if (hybrid)
                        {
                            for (const Wire::Rect& rect : m_pendingVideo)
                            {
                                AddDirty(m_pendingStill, rect);
                            }
                        }
                        m_pendingVideo.clear();
                    }
                }
            }

            // Hybrid: what went out as video and has been still long enough goes again, lossless.
            if (hybrid && m_pendingVideo.empty() && !m_pendingStill.empty() &&
                now - m_lastChangeAt >= static_cast<int64_t>(m_options.stillDelayMs) * 1000)
            {
                std::vector<Wire::Rect> absolute;
                for (const Wire::Rect& rect : m_pendingStill)
                {
                    absolute.push_back({rect.x + area.x, rect.y + area.y, rect.width, rect.height});
                }
                m_pendingStill.clear();
                if (!SendStills(capture, absolute, stillFrameId))
                {
                    return Result::SendFailed;
                }
            }
            if (m_options.logStats)
            {
                LogStats(now);
            }
            return Result::Handled;
        }

        void VideoSession::LogStats(int64_t now)
        {
            if (m_statsWindowStart == 0)
            {
                m_statsWindowStart = now;
                m_statsAtWindowStart = m_stats;
                return;
            }
            const int64_t elapsed = now - m_statsWindowStart;
            if (elapsed < static_cast<int64_t>(m_options.statsIntervalMs) * 1000)
            {
                return;
            }
            const double seconds = static_cast<double>(elapsed) / 1e6;
            const uint64_t frames = m_stats.framesSent - m_statsAtWindowStart.framesSent;
            const uint64_t bytes = m_stats.videoBytes - m_statsAtWindowStart.videoBytes;
            const double toSend = m_stats.captureToSendTotalMs - m_statsAtWindowStart.captureToSendTotalMs;
            const double encode = m_stats.encodeTotalMs - m_statsAtWindowStart.encodeTotalMs;
            LOG_INFO("Video",
                     "Server: %.1f frames a second, %.0f kbit/s (target %u), capture to send %.2f ms average "
                     "(worst %.2f), encode %.2f ms average, %llu keyframes, %llu stills",
                     frames / seconds, bytes * 8.0 / 1000.0 / seconds, static_cast<unsigned>(CurrentBitrateKbps()),
                     frames > 0 ? toSend / frames : 0.0, m_windowWorstMs, frames > 0 ? encode / frames : 0.0,
                     static_cast<unsigned long long>(m_stats.keyframesSent - m_statsAtWindowStart.keyframesSent),
                     static_cast<unsigned long long>(m_stats.stillMessages - m_statsAtWindowStart.stillMessages));
            m_statsWindowStart = now;
            m_statsAtWindowStart = m_stats;
            m_windowWorstMs = 0; // the worst shown is per window
        }
    } // namespace Server
} // namespace Marconi
