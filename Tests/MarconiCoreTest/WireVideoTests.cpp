// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the Marconi protocol's video messages: every message round-trips, every refused value is refused, a cut-off
// message never decodes as something else, and random bytes never crash a decoder

#include "TestSupport.h"

#include <MarconiCore/Wire/WireVideoMessages.h>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // Encodes, checks the envelope and the type, decodes, checks the whole body was used, returns the copy.
        template <class Message>
        Message RoundTripVideo(const Message& message, Wire::Type expected)
        {
            Wire::Bytes framed = message.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            CHECK(length == outer.Remaining());
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == expected);
            Message back = Message::Decode(body);
            CHECK(body.AtEnd());
            return back;
        }

        // The body of a framed message (length and type byte removed).
        Wire::Bytes BodyOfVideo(const Wire::Bytes& framed)
        {
            Wire::Reader outer(framed);
            outer.Varint();
            return Wire::Bytes(framed.end() - static_cast<ptrdiff_t>(outer.Remaining()) + 1, framed.end());
        }

        template <class Message>
        bool DecodeThrows(const Wire::Bytes& body)
        {
            return ThrowsWireError([&]() {
                Wire::Reader reader(body);
                Message::Decode(reader);
            });
        }

        // xorshift32 with a fixed seed: the same bytes every run, so a failure replays.
        uint32_t NextRandom(uint32_t& state)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
    } // namespace

    void RunWireVideoTests()
    {
        RunTest("video message numbers are the documented ones and do not clash", []() {
            CHECK(static_cast<int>(Wire::Type::VideoOffer) == 100);
            CHECK(static_cast<int>(Wire::Type::VideoStart) == 101);
            CHECK(static_cast<int>(Wire::Type::VideoFrame) == 102);
            CHECK(static_cast<int>(Wire::Type::VideoRequestKeyframe) == 103);
            CHECK(static_cast<int>(Wire::Type::VideoFeedback) == 104);
            CHECK(static_cast<int>(Wire::Type::VideoStop) == 105);
            CHECK(Wire::Capability::Video == 1u); // the capability bit already existed
        });

        RunTest("video offer, start and stop round-trip", []() {
            Wire::VideoOffer offer;
            offer.codecs = {Wire::VideoCodec::H264, Wire::VideoCodec::Hevc, static_cast<Wire::VideoCodec>(200)};
            offer.maxWidth = 3840;
            offer.maxHeight = 2160;
            offer.maxFps = 144;
            Wire::VideoOffer offerBack = RoundTripVideo(offer, Wire::Type::VideoOffer);
            CHECK(offerBack.codecs == offer.codecs); // an unknown codec number survives, so newer peers can offer more
            CHECK(offerBack.maxWidth == 3840 && offerBack.maxHeight == 2160 && offerBack.maxFps == 144);

            Wire::VideoStart start;
            start.streamId = 7;
            start.codec = Wire::VideoCodec::H264;
            start.width = 1920;
            start.height = 1080;
            start.fps = 60;
            start.bitrateKbps = 12000;
            start.origin = {1920, -40, 1920, 1080};
            start.extradata = {0, 0, 0, 1, 0x67, 1, 2, 3};
            Wire::VideoStart startBack = RoundTripVideo(start, Wire::Type::VideoStart);
            CHECK(startBack.streamId == 7 && startBack.codec == Wire::VideoCodec::H264);
            CHECK(startBack.width == 1920 && startBack.height == 1080 && startBack.fps == 60);
            CHECK(startBack.bitrateKbps == 12000);
            CHECK(startBack.origin.x == 1920 && startBack.origin.y == -40 && startBack.origin.width == 1920);
            CHECK(startBack.extradata == start.extradata);

            Wire::VideoStop stop;
            stop.streamId = 7;
            stop.reason = Wire::VideoStop::Reason::NoCommonCodec;
            Wire::VideoStop stopBack = RoundTripVideo(stop, Wire::Type::VideoStop);
            CHECK(stopBack.streamId == 7 && stopBack.reason == Wire::VideoStop::Reason::NoCommonCodec);
        });

        RunTest("video frames carry the id, flags, timestamps, rectangles and payload", []() {
            Wire::VideoFrame frame;
            frame.streamId = 3;
            frame.frameId = 0x1122334455ull;
            frame.flags = Wire::VideoFrame::kKeyframe;
            frame.captureMicros = 9876543210ull;
            frame.encodeMicros = 2500;
            frame.applyRects = {{0, 0, 64, 64}, {100, 200, 5, 6}};
            frame.payload = Wire::Bytes(5000, 0xAB);
            Wire::VideoFrame back = RoundTripVideo(frame, Wire::Type::VideoFrame);
            CHECK(back.streamId == 3 && back.frameId == 0x1122334455ull && back.IsKeyframe());
            CHECK(back.captureMicros == 9876543210ull && back.encodeMicros == 2500);
            CHECK(back.applyRects.size() == 2 && back.applyRects[1].x == 100 && back.applyRects[1].height == 6);
            CHECK(back.payload == frame.payload);

            Wire::VideoFrame plain;
            plain.streamId = 1;
            plain.frameId = 2;
            Wire::VideoFrame plainBack = RoundTripVideo(plain, Wire::Type::VideoFrame);
            CHECK(!plainBack.IsKeyframe() && plainBack.applyRects.empty() && plainBack.payload.empty());
        });

        RunTest("keyframe requests and feedback round-trip", []() {
            Wire::VideoRequestKeyframe request;
            request.streamId = 4;
            request.reason = Wire::VideoRequestKeyframe::Reason::DecodeError;
            request.lastGoodFrameId = 99;
            Wire::VideoRequestKeyframe requestBack = RoundTripVideo(request, Wire::Type::VideoRequestKeyframe);
            CHECK(requestBack.streamId == 4 && requestBack.lastGoodFrameId == 99);
            CHECK(requestBack.reason == Wire::VideoRequestKeyframe::Reason::DecodeError);

            Wire::VideoFeedback feedback;
            feedback.streamId = 4;
            feedback.lastFrameId = 1234567;
            feedback.framesReceived = 30;
            feedback.framesDropped = 2;
            feedback.decodeMicros = 3100;
            feedback.queueDepth = 1;
            feedback.targetBitrateKbps = 6000;
            feedback.maxFps = 30;
            feedback.quality = 80;
            Wire::VideoFeedback back = RoundTripVideo(feedback, Wire::Type::VideoFeedback);
            CHECK(back.streamId == 4 && back.lastFrameId == 1234567 && back.framesReceived == 30);
            CHECK(back.framesDropped == 2 && back.decodeMicros == 3100 && back.queueDepth == 1);
            CHECK(back.targetBitrateKbps == 6000 && back.maxFps == 30 && back.quality == 80);
        });

        RunTest("video messages with impossible values are refused", []() {
            Wire::VideoOffer tooMany;
            tooMany.codecs.assign(Wire::kMaxVideoCodecs + 1, Wire::VideoCodec::H264);
            CHECK(DecodeThrows<Wire::VideoOffer>(BodyOfVideo(tooMany.Encode())));

            Wire::VideoOffer huge;
            huge.codecs = {Wire::VideoCodec::H264};
            huge.maxWidth = Wire::kMaxVideoDimension + 1;
            CHECK(DecodeThrows<Wire::VideoOffer>(BodyOfVideo(huge.Encode())));

            Wire::VideoStart empty; // zero size
            empty.codec = Wire::VideoCodec::H264;
            CHECK(DecodeThrows<Wire::VideoStart>(BodyOfVideo(empty.Encode())));
            Wire::VideoStart big;
            big.width = 100000;
            big.height = 100;
            CHECK(DecodeThrows<Wire::VideoStart>(BodyOfVideo(big.Encode())));
            Wire::VideoStart bigSetup;
            bigSetup.width = 16;
            bigSetup.height = 16;
            bigSetup.extradata.assign(Wire::kMaxVideoExtradata + 1, 1);
            CHECK(DecodeThrows<Wire::VideoStart>(BodyOfVideo(bigSetup.Encode())));

            Wire::VideoFrame crowded;
            crowded.applyRects.assign(Wire::kMaxVideoApplyRects + 1, Wire::Rect{0, 0, 1, 1});
            CHECK(DecodeThrows<Wire::VideoFrame>(BodyOfVideo(crowded.Encode())));

            Wire::VideoFeedback wish;
            wish.quality = 101;
            CHECK(DecodeThrows<Wire::VideoFeedback>(BodyOfVideo(wish.Encode())));
        });

        RunTest("a cut-off video message never decodes", []() {
            Wire::VideoFrame frame;
            frame.streamId = 1;
            frame.frameId = 2;
            frame.applyRects = {{1, 2, 3, 4}};
            frame.payload = Wire::Bytes(20, 7);
            Wire::VideoStart start;
            start.width = 16;
            start.height = 16;
            start.extradata = {1, 2, 3};
            Wire::VideoOffer offer;
            offer.codecs = {Wire::VideoCodec::H264};
            const Wire::Bytes frameBody = BodyOfVideo(frame.Encode());
            const Wire::Bytes startBody = BodyOfVideo(start.Encode());
            const Wire::Bytes offerBody = BodyOfVideo(offer.Encode());
            for (size_t cut = 0; cut < frameBody.size(); ++cut)
            {
                CHECK(DecodeThrows<Wire::VideoFrame>(Wire::Bytes(frameBody.begin(), frameBody.begin() + cut)));
            }
            for (size_t cut = 0; cut < startBody.size(); ++cut)
            {
                CHECK(DecodeThrows<Wire::VideoStart>(Wire::Bytes(startBody.begin(), startBody.begin() + cut)));
            }
            for (size_t cut = 0; cut < offerBody.size(); ++cut)
            {
                CHECK(DecodeThrows<Wire::VideoOffer>(Wire::Bytes(offerBody.begin(), offerBody.begin() + cut)));
            }
        });

        RunTest("random bytes fed to the video decoders are decoded or refused, never crash", []() {
            uint32_t state = 0xBADC0DEu;
            auto randomBytes = [&state](size_t count) {
                Wire::Bytes bytes(count);
                for (uint8_t& byte : bytes)
                {
                    byte = static_cast<uint8_t>(NextRandom(state) >> 24);
                }
                return bytes;
            };
            for (int i = 0; i < 3000; ++i)
            {
                Wire::Bytes body = randomBytes(NextRandom(state) % 96);
                try { Wire::Reader r(body); Wire::VideoOffer::Decode(r); } catch (const Wire::WireError&) {}
                try { Wire::Reader r(body); Wire::VideoStart::Decode(r); } catch (const Wire::WireError&) {}
                try { Wire::Reader r(body); Wire::VideoFrame::Decode(r); } catch (const Wire::WireError&) {}
                try { Wire::Reader r(body); Wire::VideoRequestKeyframe::Decode(r); } catch (const Wire::WireError&) {}
                try { Wire::Reader r(body); Wire::VideoFeedback::Decode(r); } catch (const Wire::WireError&) {}
                try { Wire::Reader r(body); Wire::VideoStop::Decode(r); } catch (const Wire::WireError&) {}
            }
            // Mutating a valid frame (a flipped bit anywhere) must also be decoded or refused.
            Wire::VideoFrame frame;
            frame.streamId = 1;
            frame.frameId = 2;
            frame.applyRects = {{1, 2, 3, 4}, {5, 6, 7, 8}};
            frame.payload = Wire::Bytes(40, 9);
            const Wire::Bytes valid = BodyOfVideo(frame.Encode());
            for (size_t at = 0; at < valid.size(); ++at)
            {
                for (int flip = 0; flip < 8; ++flip)
                {
                    Wire::Bytes mutated = valid;
                    mutated[at] ^= static_cast<uint8_t>(1u << flip);
                    try { Wire::Reader r(mutated); Wire::VideoFrame::Decode(r); } catch (const Wire::WireError&) {}
                }
            }
            CHECK(true); // reaching here without a crash or a foreign exception is the result
        });
    }
} // namespace MarconiTest
