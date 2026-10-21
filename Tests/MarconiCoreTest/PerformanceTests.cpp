// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for timing pictures: comparing the server's clock with ours, and summing up each update's stages

#include "TestSupport.h"

#include <MarconiCore/Performance.h>
#include <MarconiCore/Wire/WireClockSync.h>

#include <cmath>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        bool Near(double a, double b, double tolerance = 1e-6)
        {
            return std::fabs(a - b) <= tolerance;
        }

        // A frame received at `receivedMs` on this computer, captured `ageMs` earlier, `bytes` long.
        FrameTiming Frame(uint64_t id, int64_t receivedMs, int64_t ageMs, uint64_t bytes)
        {
            FrameTiming frame;
            frame.frameId = id;
            frame.bytes = bytes;
            frame.receivedMicros = receivedMs * 1000;
            frame.beganMicros = frame.receivedMicros - 1000;
            frame.decodeMicros = 500;
            frame.captureMicros = (receivedMs - ageMs) * 1000;
            frame.sentMicros = frame.captureMicros + 2000;
            frame.compressMicros = 1500;
            return frame;
        }
    } // namespace

    void RunPerformanceTests()
    {
        RunTest("the clock offset and round trip come out of one even exchange", []() {
            // The server's clock is a second ahead; the network takes 2 ms each way and the server 0.1 ms to answer.
            Wire::ClockSync clock;
            CHECK(!clock.HasEstimate());
            CHECK(clock.GetBestRoundTripMicros() == -1);
            const uint64_t t0 = 1000;
            const uint64_t t1 = t0 + 2000 + 1000000;
            const uint64_t t2 = t1 + 100;
            const uint64_t t3 = t0 + 2000 + 100 + 2000;
            clock.AddSample(t0, t1, t2, t3);
            CHECK(clock.HasEstimate());
            CHECK(clock.GetOffsetMicros() == 1000000);
            CHECK(clock.GetBestRoundTripMicros() == 4000);
            // A server time converts to the moment it was on our clock.
            CHECK(clock.ToLocalMicros(t1) == static_cast<int64_t>(t0 + 2000));
        });

        RunTest("the clock comparison trusts the quickest recent exchange", []() {
            Wire::ClockSync clock(4);
            // An even exchange: offset exactly 500 us.
            clock.AddSample(0, 1500, 1500, 2000);
            CHECK(clock.GetOffsetMicros() == 500);
            // A lopsided one, where the answer waited 30 ms behind a picture before leaving: its offset is wrong,
            // but its round trip is longer, so it isn't used.
            clock.AddSample(10000, 11500, 11500, 42000);
            CHECK(clock.GetOffsetMicros() == 500);
            CHECK(clock.GetLatestRoundTripMicros() == 32000);
            CHECK(clock.GetBestRoundTripMicros() == 2000);
            // Once the good sample falls out of the window, the best of what's left is used.
            clock.AddSample(50000, 51500 + 4000, 51500 + 4000, 52000 + 8000); // 10 ms round trip
            clock.AddSample(60000, 61500 + 4000, 61500 + 4000, 62000 + 8000);
            clock.AddSample(70000, 71500 + 4000, 71500 + 4000, 72000 + 8000);
            CHECK(clock.GetBestRoundTripMicros() == 10000);
        });

        RunTest("an exchange whose times run backwards is ignored", []() {
            Wire::ClockSync clock;
            clock.AddSample(5000, 100, 50, 6000); // the server answered before it received
            clock.AddSample(5000, 100, 200, 4000); // the reply arrived before the ping left
            CHECK(!clock.HasEstimate());
        });

        RunTest("updates are summed up into rates and stage times", []() {
            PerformanceMonitor monitor;
            // Twenty updates over one second, 1250 bytes each, each 10 ms old when received.
            for (int i = 0; i < 20; ++i)
            {
                monitor.FrameReceived(Frame(static_cast<uint64_t>(i), 1000 + i * 50, 10, 1250));
            }
            PerformanceSnapshot s = monitor.Summarise(2000 * 1000, 1000 * 1000);
            CHECK(s.frames == 20);
            CHECK(Near(s.framesPerSecond, 20));
            CHECK(Near(s.kilobitsPerSecond, 20 * 1250 * 8 / 1000.0));
            CHECK(Near(s.serverMs, 2));
            CHECK(Near(s.compressMs, 1.5));
            CHECK(Near(s.networkMs, 8));
            CHECK(Near(s.decodeMs, 0.5));
            // Nothing painted yet: no show time, and the picture's age is measured to its arrival.
            CHECK(s.showMs == -1);
            CHECK(Near(s.pipelineMs, 10));
            CHECK(s.shownPerSecond == 0);
        });

        RunTest("a paint shows every update received since the last one", []() {
            PerformanceMonitor monitor;
            monitor.FrameReceived(Frame(1, 100, 10, 100));
            monitor.FrameReceived(Frame(2, 104, 10, 100));
            std::vector<FrameTiming> shown = monitor.Shown(110 * 1000);
            CHECK(shown.size() == 2);
            CHECK(shown.size() == 2 && shown[0].frameId == 1 && shown[1].frameId == 2);
            // A paint with nothing new shows nothing and isn't counted.
            CHECK(monitor.Shown(111 * 1000).empty());
            monitor.FrameReceived(Frame(3, 120, 10, 100));
            shown = monitor.Shown(125 * 1000);
            CHECK(shown.size() == 1 && shown[0].frameId == 3);

            PerformanceSnapshot s = monitor.Summarise(200 * 1000, 1000 * 1000);
            CHECK(s.frames == 3);
            CHECK(Near(s.shownPerSecond, 2)); // two paints showed something new
            // Shown 10, 6 and 5 ms after arriving; captured 10 ms before that: ages 20, 16 and 15 ms.
            CHECK(Near(s.showMs, 6));
            CHECK(Near(s.pipelineMs, 16));
            CHECK(Near(s.pipelineP99Ms, 20));
        });

        RunTest("updates from a server that sends no times still count", []() {
            PerformanceMonitor monitor;
            FrameTiming frame;
            frame.frameId = 1;
            frame.bytes = 500;
            frame.receivedMicros = 50000;
            frame.decodeMicros = 300;
            monitor.FrameReceived(frame);
            PerformanceSnapshot s = monitor.Summarise(60000, 1000 * 1000);
            CHECK(s.frames == 1);
            CHECK(Near(s.decodeMs, 0.3));
            CHECK(s.serverMs == -1);
            CHECK(s.networkMs == -1);
            CHECK(s.pipelineMs == -1);
        });

        RunTest("percentiles pick the value at that rank", []() {
            std::vector<double> none;
            CHECK(Percentile(none, 0.5) == -1);
            std::vector<double> values{5, 1, 4, 2, 3};
            CHECK(Percentile(values, 0.5) == 3);
            CHECK(Percentile(values, 0.99) == 5);
            CHECK(Percentile(values, 0) == 1);
        });
    }
} // namespace MarconiTest
