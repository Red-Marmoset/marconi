// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The video pipeline, end to end with the test codec: negotiation, keyframes, a lost frame and a lost keyframe,
// size changes, video and stills together, fallback to stills, a viewer that goes away, bitrate hints and pacing.
//
// Most tests drive a VideoSession and a VideoReceiver straight at each other on one thread with a fake clock
// ("Loopback"), so they are deterministic and fast. A few run the real Session over an in-memory pipe, and one runs
// the real client against the real listener over TLS on the loopback address.

#include "TestSupport.h"
#include "TestVideoCodec.h"

#include <MarconiCore/Video/VideoReceiver.h>
#include <MarconiCore/Wire/WireClient.h>
#include <MarconiCore/Wire/WireCompress.h>
#include <MarconiCore/Wire/WireVideoMessages.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>
#include <MarconiServerCore/VideoRateControl.h>
#include <MarconiServerCore/VideoSession.h>

#include <Log/Log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        struct Message
        {
            Wire::Type type;
            Wire::Bytes body; // after the type byte
        };

        Message Parse(const Wire::Bytes& framed)
        {
            Wire::Reader outer(framed);
            outer.Varint();
            Wire::Bytes rest(framed.end() - static_cast<ptrdiff_t>(outer.Remaining()), framed.end());
            Message message;
            message.type = static_cast<Wire::Type>(rest.at(0));
            message.body.assign(rest.begin() + 1, rest.end());
            return message;
        }

        uint32_t TestPixel(int x, int y, int seed)
        {
            return (static_cast<uint32_t>((x * 7 + seed) & 0xFF) << 16) |
                   (static_cast<uint32_t>((y * 5 + seed * 3) & 0xFF) << 8) |
                   static_cast<uint32_t>((x + y + seed * 11) & 0xFF);
        }

        // A capture with a picture that has plenty of low bits (so the lossy codec visibly differs). `dirty` is the
        // whole desktop unless told otherwise.
        Server::Capture MakeCapture(int width, int height, int seed, bool wholeDirty = true)
        {
            Server::Capture capture;
            capture.width = width;
            capture.height = height;
            capture.pixels.resize(static_cast<size_t>(width) * height);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    capture.pixels[static_cast<size_t>(y) * width + x] = TestPixel(x, y, seed);
                }
            }
            if (wholeDirty)
            {
                capture.dirty.push_back({0, 0, width, height});
            }
            return capture;
        }

        void Repaint(Server::Capture& capture, const Wire::Rect& rect, int seed)
        {
            for (int y = rect.y; y < rect.y + rect.height; ++y)
            {
                for (int x = rect.x; x < rect.x + rect.width; ++x)
                {
                    capture.pixels[static_cast<size_t>(y) * capture.width + x] = TestPixel(x, y, seed);
                }
            }
            capture.dirty.assign(1, rect);
        }

        // The viewer's side of a connection, fed the server's messages: a framebuffer, a receiver with the test
        // decoder, and a record of what arrived. Guarded, since the threaded tests read it from another thread.
        class ClientSide
        {
        public:
            ClientSide(int width, int height, std::shared_ptr<CodecProbe> probe, Video::VideoReceiver::Send toServer,
                       Video::VideoReceiver::Clock clock)
                : m_width(width), m_height(height), m_framebuffer(static_cast<size_t>(width) * height, 0)
            {
                m_receiver = std::make_unique<Video::VideoReceiver>(MakeTestDecoderFactory(std::move(probe)),
                                                                    std::move(toServer), std::move(clock));
                m_receiver->SetOfferedCodecs({Wire::VideoCodec::Test});
            }

            Video::VideoReceiver& Receiver() { return *m_receiver; }

            void Resize(int width, int height)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_width = width;
                m_height = height;
                m_framebuffer.assign(static_cast<size_t>(width) * height, 0);
            }

            // Takes one of the server's framed messages.
            void Handle(const Wire::Bytes& framed)
            {
                Message message = Parse(framed);
                Wire::Reader reader(message.body);
                std::lock_guard<std::mutex> lock(m_mutex);
                switch (message.type)
                {
                case Wire::Type::VideoStart:
                {
                    Wire::VideoStart start = Wire::VideoStart::Decode(reader);
                    starts.push_back(start);
                    m_receiver->OnStart(start);
                    break;
                }
                case Wire::Type::VideoFrame:
                {
                    Wire::VideoFrame frame = Wire::VideoFrame::Decode(reader);
                    frames.push_back(frame);
                    if (dropFilter && dropFilter(frame))
                    {
                        ++dropped;
                        break;
                    }
                    Video::VideoUpdate update;
                    if (m_receiver->OnFrame(frame, update))
                    {
                        for (const Wire::Rect& rect : update.rects)
                        {
                            for (int y = rect.y; y < rect.y + rect.height; ++y)
                            {
                                for (int x = rect.x; x < rect.x + rect.width; ++x)
                                {
                                    m_framebuffer[static_cast<size_t>(y) * m_width + x] =
                                        update.picture->pixels[static_cast<size_t>(y - update.origin.y) *
                                                                   update.picture->width +
                                                               (x - update.origin.x)];
                                }
                            }
                        }
                    }
                    m_receiver->MaybeSendFeedback();
                    break;
                }
                case Wire::Type::VideoStop:
                {
                    Wire::VideoStop stop = Wire::VideoStop::Decode(reader);
                    stops.push_back(stop);
                    m_receiver->OnStop(stop);
                    break;
                }
                case Wire::Type::Still:
                {
                    Wire::Still still = Wire::Still::Decode(reader);
                    stills.push_back(still.rect);
                    Wire::Bytes raw = still.data;
                    if (still.compression == Wire::Still::Compression::Zlib)
                    {
                        raw.clear();
                        Wire::ZlibDecompress(still.data, raw, static_cast<size_t>(still.rect.width) * still.rect.height * 4);
                    }
                    for (int y = 0; y < still.rect.height; ++y)
                    {
                        for (int x = 0; x < still.rect.width; ++x)
                        {
                            const uint8_t* p = raw.data() + (static_cast<size_t>(y) * still.rect.width + x) * 4;
                            m_framebuffer[static_cast<size_t>(still.rect.y + y) * m_width + still.rect.x + x] =
                                static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16);
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }

            // How many pixels of `rect` differ from `expected(x, y)`.
            int Mismatches(const Wire::Rect& rect, const std::function<uint32_t(int, int)>& expected)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int bad = 0;
                for (int y = rect.y; y < rect.y + rect.height; ++y)
                {
                    for (int x = rect.x; x < rect.x + rect.width; ++x)
                    {
                        bad += m_framebuffer[static_cast<size_t>(y) * m_width + x] != expected(x, y) ? 1 : 0;
                    }
                }
                return bad;
            }

            uint32_t Pixel(int x, int y)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_framebuffer[static_cast<size_t>(y) * m_width + x];
            }

            size_t FrameCount()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return frames.size();
            }
            size_t StillCount()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return stills.size();
            }

            // Recorded; read these only when no other thread is feeding the object (or through the counters above).
            std::vector<Wire::VideoStart> starts;
            std::vector<Wire::VideoFrame> frames;
            std::vector<Wire::VideoStop> stops;
            std::vector<Wire::Rect> stills;
            int dropped = 0;
            std::function<bool(const Wire::VideoFrame&)> dropFilter; // true = this frame is lost on the way

        private:
            std::mutex m_mutex;
            int m_width;
            int m_height;
            std::vector<uint32_t> m_framebuffer;
            std::unique_ptr<Video::VideoReceiver> m_receiver;
        };

        // A VideoSession and a viewer joined directly, with a fake clock. Everything runs on the calling thread.
        class Loopback
        {
        public:
            explicit Loopback(int width = 64, int height = 48,
                              std::vector<Wire::VideoCodec> serverCodecs = {Wire::VideoCodec::Test},
                              Server::VideoSessionOptions options = Server::VideoSessionOptions())
                : probe(std::make_shared<CodecProbe>()), m_width(width), m_height(height)
            {
                auto clockFn = [this]() { return m_now; };
                client = std::make_unique<ClientSide>(
                    width, height, probe,
                    [this](const Wire::Bytes& framed) { return ToServer(framed); }, clockFn);
                session = std::make_unique<Server::VideoSession>(
                    MakeTestEncoderFactory(probe), std::move(serverCodecs),
                    [this](const Wire::Bytes& framed) {
                        m_now += sendCostMicros;
                        client->Handle(framed);
                        return true;
                    },
                    clockFn, options);
            }

            // The viewer asks for video.
            void Offer(Wire::StreamConfig::Mode mode = Wire::StreamConfig::Mode::Hybrid, uint32_t maxKbps = 0,
                       uint16_t maxFps = 0)
            {
                Video::VideoPreferences preferences;
                preferences.mode = mode;
                preferences.maxBitrateKbps = maxKbps;
                preferences.maxFps = maxFps;
                CHECK(client->Receiver().SendOffer(preferences));
            }

            void Tick(int milliseconds) { m_now += static_cast<int64_t>(milliseconds) * 1000; }

            Server::VideoSession::Result Step(const Server::Capture& capture, bool full = false)
            {
                return StepArea(capture, {0, 0, capture.width, capture.height}, full);
            }
            Server::VideoSession::Result StepArea(const Server::Capture& capture, const Wire::Rect& area,
                                                  bool full = false)
            {
                return session->Process(capture, area, full, m_stillFrameId);
            }

            int64_t Now() const { return m_now; }

            std::shared_ptr<CodecProbe> probe;
            std::unique_ptr<ClientSide> client;
            std::unique_ptr<Server::VideoSession> session;
            std::vector<Message> toServer; // what the viewer sent, in order
            int64_t sendCostMicros = 0;    // the fake clock moves this much on every message the server sends

        private:
            bool ToServer(const Wire::Bytes& framed)
            {
                Message message = Parse(framed);
                toServer.push_back(message);
                Wire::Reader reader(message.body);
                switch (message.type)
                {
                case Wire::Type::VideoOffer: session->OnOffer(Wire::VideoOffer::Decode(reader)); break;
                case Wire::Type::StreamConfig: session->OnStreamConfig(Wire::StreamConfig::Decode(reader)); break;
                case Wire::Type::VideoRequestKeyframe:
                    session->OnKeyframeRequest(Wire::VideoRequestKeyframe::Decode(reader));
                    break;
                case Wire::Type::VideoFeedback: session->OnFeedback(Wire::VideoFeedback::Decode(reader)); break;
                case Wire::Type::VideoStop: session->OnViewerStop(Wire::VideoStop::Decode(reader)); break;
                default: break;
                }
                return true;
            }

            int64_t m_now = 5000000;
            int m_width;
            int m_height;
            uint64_t m_stillFrameId = 0;
        };

        using Result = Server::VideoSession::Result;

        std::function<uint32_t(int, int)> Quantized(int seed)
        {
            return [seed](int x, int y) { return TestCodecQuantize(TestPixel(x, y, seed)); };
        }
        std::function<uint32_t(int, int)> Exact(int seed)
        {
            return [seed](int x, int y) { return TestPixel(x, y, seed); };
        }

        int CountMessages(const Loopback& loopback, Wire::Type type)
        {
            int count = 0;
            for (const Message& message : loopback.toServer)
            {
                count += message.type == type ? 1 : 0;
            }
            return count;
        }

        bool WaitUntilTrue(const std::function<bool()>& predicate, int timeoutMs = 8000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return predicate();
        }

        // Log lines captured while a test runs.
        std::mutex g_logMutex;
        std::vector<std::string> g_logLines;
        void CaptureLog(Log::Level, const char* tag, const char* message)
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            g_logLines.push_back(std::string(tag) + ": " + message);
        }
    } // namespace

    void RunVideoTests()
    {
        // ---- Rate control -------------------------------------------------------------------------------------

        RunTest("Rate control: the lowest wish is the ceiling, congestion cuts the rate, clean reports raise it", []() {
            Server::VideoRateControl rate(40000, 8000, 60);
            CHECK(rate.Current() == 8000);
            rate.SetConfigMaximum(5000);
            CHECK(rate.Current() == 5000); // the viewer's maximum applies at once
            Wire::VideoFeedback wish;
            wish.framesReceived = 30;
            wish.targetBitrateKbps = 2000;
            CHECK(rate.OnFeedback(wish) && rate.Current() == 2000); // the lower of the two wishes
            Wire::VideoFeedback bad = wish;
            bad.framesDropped = 2;
            CHECK(rate.OnFeedback(bad) && rate.Current() == 1500); // three quarters
            Wire::VideoFeedback slow = wish;
            slow.decodeMicros = 16000; // nearly the whole 16.7 ms frame
            CHECK(rate.OnFeedback(slow) && rate.Current() == 1125);
            Wire::VideoFeedback queued = wish;
            queued.queueDepth = 3;
            rate.OnFeedback(queued);
            CHECK(rate.Current() == 843);
            for (int i = 0; i < 40; ++i)
            {
                rate.OnFeedback(bad);
            }
            CHECK(rate.Current() == Server::VideoRateControl::kFloorKbps); // never below the floor
            for (int i = 0; i < 3; ++i)
            {
                CHECK(!rate.OnFeedback(wish)); // three clean reports are not enough yet
            }
            CHECK(rate.OnFeedback(wish) && rate.Current() > Server::VideoRateControl::kFloorKbps);
            for (int i = 0; i < 400; ++i)
            {
                rate.OnFeedback(wish);
            }
            CHECK(rate.Current() == 2000); // climbs back, and stops at the viewer's target
            wish.targetBitrateKbps = 0;
            wish.quality = 1;
            for (int i = 0; i < 4; ++i)
            {
                rate.OnFeedback(wish);
            }
            CHECK(rate.Ceiling() == 5000 * 25 / 100); // a low quality wish scales the ceiling down
        });

        // ---- Negotiation and the first picture ---------------------------------------------------------------

        RunTest("Video: negotiation picks the viewer's first codec the server has, and the first picture is a keyframe", []() {
            Loopback loop(64, 48, {Wire::VideoCodec::H264, Wire::VideoCodec::Test});
            CHECK(!loop.session->WantsFrames()); // nothing asked for yet: stills as always
            loop.client->Receiver().SetOfferedCodecs({Wire::VideoCodec::Hevc, Wire::VideoCodec::Test});
            loop.Offer();
            CHECK(loop.session->WantsFrames());
            Server::Capture capture = MakeCapture(64, 48, 1);
            CHECK(loop.Step(capture, true) == Result::Handled);

            CHECK(loop.client->starts.size() == 1);
            const Wire::VideoStart& start = loop.client->starts[0];
            CHECK(start.codec == Wire::VideoCodec::Test); // HEVC is the viewer's first but the server has none
            CHECK(start.streamId == 1 && start.width == 64 && start.height == 48);
            CHECK(start.origin.x == 0 && start.origin.width == 64 && start.origin.height == 48);
            CHECK(start.extradata == Wire::Bytes({'T', 'E', 'S', 'T'}));
            CHECK(start.bitrateKbps >= 1000 && start.fps == 60);
            CHECK(loop.client->Receiver().IsStreaming());
            CHECK(loop.client->frames.size() == 1);
            CHECK(loop.client->frames[0].IsKeyframe() && loop.client->frames[0].frameId == 1);
            CHECK(loop.client->frames[0].captureMicros > 0);
            // Video is lossy by design, so the picture is the quantised one, everywhere.
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(1)) == 0);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Exact(1)) > 0);
            CHECK(loop.probe->encodersAlive == 1 && loop.probe->decodersAlive == 1);
        });

        RunTest("Video: a viewer asking for still only gets no video, and can switch to video and back", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::StillOnly);
            CHECK(!loop.session->WantsFrames());
            Video::VideoPreferences hybrid;
            hybrid.mode = Wire::StreamConfig::Mode::Hybrid;
            loop.client->Receiver().SetPreferences(hybrid);
            CHECK(loop.session->WantsFrames());
            Server::Capture capture = MakeCapture(64, 48, 1);
            CHECK(loop.Step(capture, true) == Result::Handled);
            CHECK(loop.client->Receiver().IsStreaming());

            Video::VideoPreferences stills;
            stills.mode = Wire::StreamConfig::Mode::StillOnly;
            loop.client->Receiver().SetPreferences(stills);
            loop.Tick(20);
            CHECK(loop.Step(capture) == Result::UseStills); // the session then sends the whole picture as stills
            CHECK(loop.client->stops.size() == 1 && loop.client->stops[0].reason == Wire::VideoStop::Reason::Switched);
            CHECK(!loop.client->Receiver().IsStreaming());
            CHECK(!loop.session->WantsFrames());
            CHECK(loop.probe->encodersAlive == 0); // the encoder went with the stream

            loop.client->Receiver().SetPreferences(hybrid);
            loop.Tick(20);
            CHECK(loop.session->WantsFrames());
            CHECK(loop.Step(capture) == Result::Handled);
            CHECK(loop.client->Receiver().IsStreaming() && loop.client->starts.size() == 2);
            CHECK(loop.client->starts[1].streamId == 2); // a new stream, so a late frame of the first is ignored
        });

        // ---- Loss and recovery -------------------------------------------------------------------------------

        RunTest("Video: a lost frame makes the viewer ask for a keyframe, and the picture recovers", []() {
            Loopback loop;
            loop.Offer();
            loop.client->dropFilter = [](const Wire::VideoFrame& frame) { return frame.frameId == 3; };
            int keyframeAt = -1;
            for (int step = 1; step <= 12; ++step)
            {
                Server::Capture capture = MakeCapture(64, 48, step);
                loop.Tick(20);
                CHECK(loop.Step(capture, step == 1) == Result::Handled);
                if (keyframeAt < 0 && step > 3 && !loop.client->frames.empty() && loop.client->frames.back().IsKeyframe())
                {
                    keyframeAt = step;
                }
            }
            CHECK(loop.client->dropped == 1);
            CHECK(loop.client->Receiver().GetStats().keyframesRequested == 1); // asked once, not once a frame
            CHECK(CountMessages(loop, Wire::Type::VideoRequestKeyframe) == 1);
            // The request came at 60 ms, inside the 100 ms minimum since the first keyframe, so the server waited.
            CHECK(keyframeAt > 4 && keyframeAt <= 8);
            CHECK(loop.client->Receiver().GetStats().framesDropped >= 2); // the lost one and the deltas after it
            CHECK(loop.probe->decodeFailures == 0); // the viewer never fed the decoder a frame it could not use
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(12)) == 0); // back to the live picture
            CHECK(loop.probe->keyframesEncoded == 2);
        });

        RunTest("Video: losing the keyframe that starts a stream is recovered the same way", []() {
            Loopback loop;
            loop.Offer();
            loop.client->dropFilter = [](const Wire::VideoFrame& frame) { return frame.frameId == 1; };
            for (int step = 1; step <= 10; ++step)
            {
                Server::Capture capture = MakeCapture(64, 48, step);
                loop.Tick(25);
                CHECK(loop.Step(capture, step == 1) == Result::Handled);
            }
            CHECK(loop.client->dropped == 1);
            CHECK(loop.client->Receiver().GetStats().keyframesRequested == 1);
            CHECK(loop.probe->decodeFailures == 0);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(10)) == 0);
        });

        RunTest("Video: a frame the decoder refuses is handled with a keyframe request, not a broken picture", []() {
            Loopback loop;
            loop.Offer();
            Server::Capture first = MakeCapture(64, 48, 1);
            loop.Step(first, true);
            // Hand the viewer a frame its decoder cannot use (a corrupt payload that still arrives in order).
            Wire::VideoFrame corrupt;
            corrupt.streamId = 1;
            corrupt.frameId = 2;
            corrupt.payload = {1, 2, 3};
            loop.client->Handle(corrupt.Encode());
            CHECK(loop.probe->decodeFailures == 1);
            CHECK(loop.client->Receiver().GetStats().keyframesRequested == 1);
            Wire::VideoRequestKeyframe request;
            for (const Message& message : loop.toServer)
            {
                if (message.type == Wire::Type::VideoRequestKeyframe)
                {
                    Wire::Reader reader(message.body);
                    request = Wire::VideoRequestKeyframe::Decode(reader);
                }
            }
            CHECK(request.reason == Wire::VideoRequestKeyframe::Reason::DecodeError && request.lastGoodFrameId == 1);
            for (int step = 2; step <= 8; ++step)
            {
                loop.Tick(30);
                loop.Step(MakeCapture(64, 48, step));
            }
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(8)) == 0);
        });

        RunTest("Video: a request for an old stream, or two requests close together, are not acted on twice", []() {
            Loopback loop;
            loop.Offer();
            loop.Step(MakeCapture(64, 48, 1), true);
            const int forcedAtStart = loop.probe->keyframesForced;
            Wire::VideoRequestKeyframe stale;
            stale.streamId = 99;
            loop.session->OnKeyframeRequest(stale);
            loop.Tick(500);
            loop.Step(MakeCapture(64, 48, 2));
            CHECK(loop.probe->keyframesForced == forcedAtStart); // ignored
            Wire::VideoRequestKeyframe real;
            real.streamId = 1;
            loop.session->OnKeyframeRequest(real);
            loop.session->OnKeyframeRequest(real);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 3));
            CHECK(loop.probe->keyframesForced == forcedAtStart + 1);
            CHECK(loop.client->frames.back().IsKeyframe());
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 4));
            CHECK(!loop.client->frames.back().IsKeyframe()); // one request, one keyframe
            CHECK(loop.session->GetStats().keyframeRequestsServed == 1);
        });

        RunTest("Video: a keyframe asked for when nothing changed copies nothing onto the screen", []() {
            Loopback loop;
            loop.Offer();
            Server::Capture capture = MakeCapture(64, 48, 1);
            loop.Step(capture, true);
            loop.Tick(400); // the lossless refresh goes out
            capture.dirty.clear();
            loop.Step(capture);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Exact(1)) == 0);
            Wire::VideoRequestKeyframe request;
            request.streamId = 1;
            loop.session->OnKeyframeRequest(request);
            loop.Tick(200);
            loop.Step(capture);
            CHECK(loop.client->frames.back().IsKeyframe());
            CHECK(loop.client->frames.back().applyRects.size() == 1 && loop.client->frames.back().applyRects[0].width == 0);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Exact(1)) == 0); // the sharp picture was left alone
        });

        // ---- Size changes -----------------------------------------------------------------------------------

        RunTest("Video: a resolution change mid-stream starts a new stream with a keyframe", []() {
            Loopback loop;
            loop.Offer();
            loop.Step(MakeCapture(64, 48, 1), true);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 2));
            CHECK(loop.client->starts.size() == 1);

            // The viewer switches to a part of the desktop (a display): a smaller picture at an offset.
            loop.Tick(20);
            CHECK(loop.StepArea(MakeCapture(64, 48, 3), {16, 8, 32, 24}) == Result::Handled);
            CHECK(loop.client->starts.size() == 2);
            CHECK(loop.client->starts[1].streamId == 2 && loop.client->starts[1].width == 32 &&
                  loop.client->starts[1].height == 24);
            CHECK(loop.client->starts[1].origin.x == 16 && loop.client->starts[1].origin.y == 8);
            CHECK(loop.client->frames.back().streamId == 2 && loop.client->frames.back().IsKeyframe());
            CHECK(loop.client->frames.back().frameId == 1); // frame numbers restart with the stream
            CHECK(loop.client->Mismatches({16, 8, 32, 24}, Quantized(3)) == 0);
            CHECK(loop.probe->encodersAlive == 1 && loop.probe->encodersCreated == 2);
            CHECK(loop.probe->configs.size() == 2 && loop.probe->configs[1].first == 32);

            // The whole desktop changes size.
            loop.client->Resize(80, 60);
            loop.Tick(20);
            CHECK(loop.Step(MakeCapture(80, 60, 4), true) == Result::Handled);
            CHECK(loop.client->starts.size() == 3 && loop.client->starts[2].width == 80);
            CHECK(loop.client->Mismatches({0, 0, 80, 60}, Quantized(4)) == 0);
            CHECK(loop.probe->decodeFailures == 0);
        });

        RunTest("Video: a picture bigger than the viewer's decoder takes is refused, and stills carry on", []() {
            Loopback loop;
            Wire::VideoOffer small;
            small.codecs = {Wire::VideoCodec::Test};
            small.maxWidth = 32;
            small.maxHeight = 32;
            loop.session->OnOffer(small);
            Wire::StreamConfig config;
            config.mode = Wire::StreamConfig::Mode::Hybrid;
            loop.session->OnStreamConfig(config);
            CHECK(loop.Step(MakeCapture(64, 48, 1), true) == Result::UseStills);
            CHECK(loop.client->stops.size() == 1);
            CHECK(!loop.session->WantsFrames());
        });

        // ---- Fallback to stills ------------------------------------------------------------------------------

        RunTest("Video: with no codec in common the server says so and the pictures stay stills", []() {
            Loopback loop(64, 48, {Wire::VideoCodec::H264});
            loop.Offer();
            CHECK(loop.session->WantsFrames());
            CHECK(loop.Step(MakeCapture(64, 48, 1), true) == Result::UseStills);
            CHECK(loop.client->stops.size() == 1 && loop.client->stops[0].reason == Wire::VideoStop::Reason::NoCommonCodec);
            CHECK(!loop.client->Receiver().IsStreaming());
            CHECK(!loop.session->WantsFrames()); // not asked again every frame
            CHECK(loop.client->starts.empty() && loop.probe->encodersCreated == 0);
        });

        RunTest("Video: an encoder that will not start leaves the viewer on stills", []() {
            Loopback loop;
            loop.probe->failConfigure = true;
            loop.Offer();
            CHECK(loop.Step(MakeCapture(64, 48, 1), true) == Result::UseStills);
            CHECK(loop.client->stops.size() == 1 && loop.client->stops[0].reason == Wire::VideoStop::Reason::EncoderFailed);
            CHECK(!loop.session->WantsFrames() && loop.probe->encodersAlive == 0);
        });

        RunTest("Video: an encoder that fails mid-stream is dropped after three tries and stills take over", []() {
            Loopback loop;
            loop.probe->failEncodeAfter = 2;
            loop.Offer();
            Result last = Result::Handled;
            int steps = 0;
            for (int step = 1; step <= 8 && last != Result::UseStills; ++step)
            {
                loop.Tick(20);
                last = loop.Step(MakeCapture(64, 48, step), step == 1);
                ++steps;
            }
            CHECK(last == Result::UseStills);
            CHECK(steps == 5); // two pictures, then three failures
            CHECK(loop.client->frames.size() == 2);
            CHECK(loop.client->stops.size() == 1 && loop.client->stops[0].reason == Wire::VideoStop::Reason::EncoderFailed);
            CHECK(!loop.client->Receiver().IsStreaming());
            CHECK(!loop.session->WantsFrames() && loop.probe->encodersAlive == 0);
        });

        RunTest("Video: a viewer whose decoder cannot start tells the server, which stops video", []() {
            Loopback loop;
            loop.probe->decoderRefusesConfigure = true;
            loop.Offer();
            CHECK(loop.Step(MakeCapture(64, 48, 1), true) == Result::Handled); // the stream began; the viewer then refused
            CHECK(CountMessages(loop, Wire::Type::VideoStop) == 1);
            CHECK(!loop.client->Receiver().IsStreaming());
            loop.Tick(20);
            CHECK(loop.Step(MakeCapture(64, 48, 2)) == Result::UseStills);
            CHECK(!loop.session->WantsFrames() && loop.probe->encodersAlive == 0);
            // A fresh offer is a fresh start (the viewer may have fixed its decoder).
            loop.probe->decoderRefusesConfigure = false;
            loop.Offer();
            CHECK(loop.session->WantsFrames());
            loop.Tick(20);
            CHECK(loop.Step(MakeCapture(64, 48, 3), true) == Result::Handled);
            CHECK(loop.client->Receiver().IsStreaming());
        });

        // ---- Video and stills together ----------------------------------------------------------------------

        RunTest("Hybrid: a part that moved is video, then goes sharp as a lossless still once it has been still", []() {
            Loopback loop;
            loop.Offer();
            Server::Capture capture = MakeCapture(64, 48, 1);
            loop.Step(capture, true);
            loop.Tick(20);
            capture = MakeCapture(64, 48, 2);
            loop.Step(capture);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(2)) == 0); // moving: video quality
            CHECK(loop.client->StillCount() == 0);

            capture.dirty.clear();
            loop.Tick(250);
            loop.Step(capture);
            CHECK(loop.client->StillCount() == 0); // 250 ms of calm is not yet enough
            loop.Tick(49);
            loop.Step(capture);
            CHECK(loop.client->StillCount() == 0); // 299 ms
            loop.Tick(1);
            loop.Step(capture);
            CHECK(loop.client->StillCount() == 1); // 300 ms: resend, lossless
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Exact(2)) == 0);
            // And only once.
            for (int i = 0; i < 20; ++i)
            {
                loop.Tick(100);
                loop.Step(capture);
            }
            CHECK(loop.client->StillCount() == 1);
            CHECK(loop.client->frames.size() == 2);
        });

        RunTest("Hybrid: while something keeps changing the sharp refresh waits", []() {
            Loopback loop;
            loop.Offer();
            for (int step = 1; step <= 30; ++step)
            {
                loop.Tick(50);
                loop.Step(MakeCapture(64, 48, step), step == 1);
            }
            CHECK(loop.client->StillCount() == 0);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(30)) == 0);
        });

        RunTest("Hybrid: a small change is a lossless still at once, and the video picture is not painted over it", []() {
            Loopback loop;
            loop.Offer();
            Server::Capture capture = MakeCapture(64, 48, 1);
            loop.Step(capture, true);
            loop.Tick(400);
            loop.Step(MakeCapture(64, 48, 1, false)); // settles into the sharp picture
            CHECK(loop.client->StillCount() == 1);
            const size_t framesBefore = loop.client->frames.size();

            // Ten by four pixels: far less than a thirty-second of the picture.
            Repaint(capture, {10, 10, 10, 4}, 7);
            loop.Tick(20);
            CHECK(loop.Step(capture) == Result::Handled);
            CHECK(loop.client->frames.size() == framesBefore); // no video frame for it
            CHECK(loop.client->StillCount() == 2);
            CHECK(loop.client->stills.back().x == 10 && loop.client->stills.back().width == 10);
            CHECK(loop.client->Mismatches({10, 10, 10, 4}, Exact(7)) == 0);

            // Now a large change moves everything as video; the next still-sized change still lands exactly.
            Server::Capture big = MakeCapture(64, 48, 9);
            loop.Tick(20);
            loop.Step(big);
            CHECK(loop.client->frames.size() == framesBefore + 1);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(9)) == 0);
        });

        RunTest("Video mode: everything is video, a small change too, and nothing is refreshed", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::Video);
            Server::Capture capture = MakeCapture(64, 48, 1);
            loop.Step(capture, true);
            Repaint(capture, {10, 10, 10, 4}, 7);
            loop.Tick(20);
            loop.Step(capture);
            CHECK(loop.client->frames.size() == 2);
            CHECK(loop.client->frames.back().applyRects.size() == 1 && loop.client->frames.back().applyRects[0].x == 10);
            CHECK(loop.client->Mismatches({10, 10, 10, 4}, Quantized(7)) == 0);
            capture.dirty.clear();
            for (int i = 0; i < 20; ++i)
            {
                loop.Tick(100);
                loop.Step(capture);
            }
            CHECK(loop.client->StillCount() == 0);
        });

        // ---- Pacing, hints and numbers ----------------------------------------------------------------------

        RunTest("Pacing: frames go out at the agreed rate however often the screen is grabbed", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::Hybrid, 0, 30);
            loop.Step(MakeCapture(64, 48, 0), true);
            for (int ms = 1; ms <= 1000; ++ms)
            {
                loop.Tick(1);
                loop.Step(MakeCapture(64, 48, ms));
            }
            const size_t at30 = loop.client->frames.size();
            CHECK(at30 >= 29 && at30 <= 32);
            CHECK(loop.client->starts[0].fps == 30);

            Loopback fast;
            fast.Offer();
            fast.Step(MakeCapture(64, 48, 0), true);
            for (int ms = 1; ms <= 1000; ++ms)
            {
                fast.Tick(1);
                fast.Step(MakeCapture(64, 48, ms));
            }
            const size_t at60 = fast.client->frames.size();
            CHECK(at60 >= 56 && at60 <= 61);
        });

        RunTest("Pacing: a change that arrives too soon waits for its slot and is not lost", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::Hybrid, 0, 10); // a frame every 100 ms
            Server::Capture first = MakeCapture(64, 48, 1);
            loop.Step(first, true);
            loop.Tick(10);
            Server::Capture second = MakeCapture(64, 48, 2);
            loop.Step(second);
            CHECK(loop.client->frames.size() == 1); // too soon
            Server::Capture quiet = second;
            quiet.dirty.clear(); // later grabs report nothing new: the change from before must still go out
            loop.Tick(95);
            loop.Step(quiet);
            CHECK(loop.client->frames.size() == 2);
            CHECK(loop.client->Mismatches({0, 0, 64, 48}, Quantized(2)) == 0);
        });

        RunTest("Bitrate: the viewer's maximum and its reports steer the encoder", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::Hybrid, 2000);
            loop.Step(MakeCapture(64, 48, 1), true);
            // A small picture starts at the 1000 kbit/s floor of the starting guess, inside the viewer's 2000.
            CHECK(loop.probe->configBitrates.size() == 1 && loop.probe->configBitrates[0] == 1000);
            CHECK(loop.session->CurrentBitrateKbps() == 1000);
            // Four clean reports raise it an eighth.
            Wire::VideoFeedback clean;
            clean.streamId = 1;
            clean.framesReceived = 20;
            clean.targetBitrateKbps = 2000;
            for (int i = 0; i < 4; ++i)
            {
                loop.session->OnFeedback(clean);
            }
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 2));
            CHECK(loop.session->CurrentBitrateKbps() == 1125);
            CHECK(!loop.probe->bitrates.empty() && loop.probe->bitrates.back() == 1125);
            // The viewer lowers its maximum while streaming: it applies at once.
            Video::VideoPreferences lower;
            lower.maxBitrateKbps = 500;
            loop.client->Receiver().SetPreferences(lower);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 3));
            CHECK(loop.session->CurrentBitrateKbps() == 500);
            CHECK(loop.probe->bitrates.back() == 500);
            // A report with lost frames: three quarters.
            Wire::VideoFeedback bad = clean;
            bad.framesDropped = 4;
            bad.targetBitrateKbps = 500;
            loop.session->OnFeedback(bad);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 4));
            CHECK(loop.session->CurrentBitrateKbps() == 375);
            CHECK(loop.probe->bitrates.back() == 375);
            // A report for a stream that is gone is ignored.
            bad.streamId = 77;
            loop.session->OnFeedback(bad);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 5));
            CHECK(loop.session->CurrentBitrateKbps() == 375);
            // A frame rate wish slows the pacing.
            Wire::VideoFeedback slowDown = clean;
            slowDown.maxFps = 15;
            slowDown.targetBitrateKbps = 500;
            loop.session->OnFeedback(slowDown);
            loop.Tick(20);
            loop.Step(MakeCapture(64, 48, 6));
            const size_t before = loop.client->frames.size();
            for (int i = 0; i < 10; ++i)
            {
                loop.Tick(20);
                loop.Step(MakeCapture(64, 48, 7 + i));
            }
            CHECK(loop.client->frames.size() - before <= 4); // 200 ms at 15 frames a second
        });

        RunTest("Bitrate: the viewer reports twice a second with its wishes, and the server hears them", []() {
            Loopback loop;
            loop.Offer(Wire::StreamConfig::Mode::Hybrid, 3000, 0);
            Video::VideoPreferences wishes;
            wishes.maxBitrateKbps = 3000;
            wishes.quality = 70;
            loop.client->Receiver().SetPreferences(wishes);
            for (int step = 1; step <= 60; ++step)
            {
                loop.Tick(20);
                loop.Step(MakeCapture(64, 48, step), step == 1);
            }
            const int reports = CountMessages(loop, Wire::Type::VideoFeedback);
            CHECK(reports >= 1 && reports <= 3); // 1.2 seconds of frames
            Wire::VideoFeedback last;
            for (const Message& message : loop.toServer)
            {
                if (message.type == Wire::Type::VideoFeedback)
                {
                    Wire::Reader reader(message.body);
                    last = Wire::VideoFeedback::Decode(reader);
                }
            }
            CHECK(last.streamId == 1 && last.targetBitrateKbps == 3000 && last.quality == 70);
            CHECK(last.framesReceived > 10 && last.framesDropped == 0);
            CHECK(loop.session->CurrentBitrateKbps() <= 3000);
        });

        RunTest("Stats: capture to send and frames a second are counted, and logged with --stats", []() {
            {
                std::lock_guard<std::mutex> lock(g_logMutex);
                g_logLines.clear();
            }
            Log::SetCallback(CaptureLog);
            Server::VideoSessionOptions options;
            options.logStats = true;
            options.statsIntervalMs = 1000;
            Loopback loop(64, 48, {Wire::VideoCodec::Test}, options);
            loop.sendCostMicros = 1500; // the fake clock moves 1.5 ms for every message the server sends
            loop.Offer();
            loop.Step(MakeCapture(64, 48, 0), true);
            for (int step = 1; step <= 150; ++step)
            {
                loop.Tick(17);
                loop.Step(MakeCapture(64, 48, step));
            }
            Log::SetCallback(nullptr);
            Server::VideoStats stats = loop.session->GetStats();
            CHECK(stats.framesSent == 151 && stats.keyframesSent == 1 && stats.streamsStarted == 1);
            CHECK(stats.videoBytes > 0);
            const double average = stats.captureToSendTotalMs / static_cast<double>(stats.framesSent);
            CHECK(average > 1.4 && average < 1.6);
            CHECK(stats.captureToSendMaxMs < 3.5); // the first frame also waited for the VideoStart message
            std::lock_guard<std::mutex> lock(g_logMutex);
            int serverLines = 0;
            bool sawNumbers = false;
            for (const std::string& line : g_logLines)
            {
                if (line.find("Video: Server:") == 0)
                {
                    ++serverLines;
                    sawNumbers = sawNumbers || (line.find("frames a second") != std::string::npos &&
                                                line.find("kbit/s") != std::string::npos &&
                                                line.find("capture to send") != std::string::npos);
                }
            }
            CHECK(serverLines >= 2 && sawNumbers);
        });

        // ---- The real Session over a pipe -------------------------------------------------------------------

        RunTest("Session: a server with no encoder does not grant video, so the viewer gets stills", []() {
            if (!Server::PlatformEncodableCodecs().empty())
            {
                return; // this platform has a real encoder: the claim below is about builds without one
            }
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            Server::Session session(serverTransport, screen, input, settings);
            session.SetFrameIntervalMs(1);
            std::thread serverThread([&]() { session.Run(); });
            Wire::MessageReader reader(clientTransport);
            Wire::Hello hello;
            hello.capabilities = Wire::Capability::Video;
            Wire::Bytes hb = hello.Encode();
            clientTransport.Write(hb.data(), hb.size());
            Wire::Bytes bytes = reader.Next();
            Wire::Reader body(bytes);
            CHECK(Wire::ReadType(body) == Wire::Type::Welcome);
            CHECK((Wire::Welcome::Decode(body).capabilities & Wire::Capability::Video) == 0);
            Wire::Bytes lb = Wire::Login{""}.Encode();
            clientTransport.Write(lb.data(), lb.size());
            session.Stop();
            serverToClient.Close();
            clientToServer.Close();
            serverThread.join();
        });

        RunTest("Session: video runs over the connection, and the encoder goes when the viewer does", []() {
            auto probe = std::make_shared<CodecProbe>();
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);
            Server::TestPatternScreen screen(32, 24);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            Server::Session session(serverTransport, screen, input, settings);
            session.SetFrameIntervalMs(2);
            session.SetVideoEncoderFactory(MakeTestEncoderFactory(probe), {Wire::VideoCodec::Test});
            std::thread serverThread([&]() { session.Run(); });

            Wire::MessageReader reader(clientTransport);
            auto send = [&](const Wire::Bytes& framed) {
                clientTransport.Write(framed.data(), framed.size());
                return true;
            };
            Wire::Hello hello;
            hello.capabilities = Wire::Capability::Video;
            send(hello.Encode());
            Wire::Bytes bytes = reader.Next();
            Wire::Reader body(bytes);
            CHECK(Wire::ReadType(body) == Wire::Type::Welcome);
            CHECK((Wire::Welcome::Decode(body).capabilities & Wire::Capability::Video) != 0);
            send(Wire::Login{""}.Encode());
            bytes = reader.Next();
            Wire::Reader result(bytes);
            CHECK(Wire::ReadType(result) == Wire::Type::LoginResult);

            ClientSide client(32, 24, probe, send, nullptr);
            CHECK(client.Receiver().SendOffer(Video::VideoPreferences()));
            std::atomic<bool> reading{true};
            std::thread readerThread([&]() {
                try
                {
                    while (reading)
                    {
                        client.Handle(Wire::Frame(reader.Next())); // Next() strips the length; Handle wants it framed
                    }
                }
                catch (const Wire::WireError& wireError)
                {
                    printf("  (viewer reader ended: %s)\n", wireError.what());
                }
            });
            CHECK(WaitUntilTrue([&]() { return client.FrameCount() >= 10; }));
            CHECK(client.Receiver().IsStreaming());
            CHECK(probe->encodersAlive == 1);
            // Wherever the test pattern's moving bar is not, the picture is the quantised pattern.
            CHECK(client.Pixel(31, 23) != 0);
            CHECK((client.Pixel(31, 23) & 0x000F0F0F) == 0 || client.Pixel(31, 23) == 0x00FFFFFF);

            // The viewer goes away mid-stream.
            reading = false;
            serverToClient.Close();
            clientToServer.Close();
            serverThread.join();
            readerThread.join();
            CHECK(probe->encodersAlive == 0); // the encoder was released with the session
            CHECK(client.Receiver().GetStats().framesDecoded >= 10);
            CHECK(probe->decodeFailures == 0);
        });

        // ---- The real client and listener over TLS ----------------------------------------------------------

        RunTest("Client and server over TLS: video starts, reaches the framebuffer, and switches back to stills", []() {
            auto probe = std::make_shared<CodecProbe>();
            Server::TestPatternScreen screen(16, 16);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true;
            settings.port = 0;
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(3);
            listener.SetVideoEncoderFactory(MakeTestEncoderFactory(probe), {Wire::VideoCodec::Test});
            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(10000); });

            RecordingClientListener listenerCallbacks;
            Wire::Client client(listenerCallbacks);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "video test";
            options.offerFiles = false;
            options.offerVideo = true;
            options.videoDecoderFactory = MakeTestDecoderFactory(probe);
            options.videoCodecs = {Wire::VideoCodec::Test};
            client.Connect(options);
            CHECK(listenerCallbacks.WaitFor([&]() { return listenerCallbacks.connected; }));
            CHECK(WaitUntilTrue([&]() { return client.SupportsVideo(); }));
            CHECK(WaitUntilTrue([&]() { return client.IsVideoStreaming(); }));
            CHECK(WaitUntilTrue([&]() { return probe->picturesDecoded >= 5; }));
            {
                // The bottom right pixel is never the moving bar here: red 255, green 255, blue the frame count, and
                // through the lossy codec the low four bits of each channel are gone from red and green.
                std::lock_guard<std::mutex> lock(client.GetFramebuffer().GetMutex());
                uint32_t pixel = client.GetFramebuffer().GetPixel(15, 15);
                CHECK((pixel & 0x00FFFF00u) == 0x00F0F000u || pixel == 0x00FFFFFFu);
            }

            // Asking for stills only ends the stream; the pictures are then exact.
            Video::VideoPreferences stills;
            stills.mode = Wire::StreamConfig::Mode::StillOnly;
            client.SetVideoPreferences(stills);
            CHECK(WaitUntilTrue([&]() { return !client.IsVideoStreaming(); }));
            CHECK(WaitUntilTrue([&]() {
                std::lock_guard<std::mutex> lock(client.GetFramebuffer().GetMutex());
                return (client.GetFramebuffer().GetPixel(15, 15) & 0x00FFFF00u) == 0x00FFFF00u;
            }));
            client.Disconnect();
            serverThread.join();
            listener.Stop();
            CHECK(probe->encodersAlive == 0);
        });
    }
} // namespace MarconiServerTest
