// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The capture and compression benchmark

#include "Bench.h"

#include "StillEncoder.h"

#include <MarconiCore/Performance.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            double Ms(uint64_t micros)
            {
                return static_cast<double>(micros) / 1000.0;
            }
        } // namespace

        BenchResult RunBench(ScreenSource& screen, const BenchOptions& options)
        {
            BenchResult result;
            std::vector<double> loop, grab, pack, compress, frame, pixels, bytes;
            uint64_t totalBytes = 0;
            const uint64_t start = Wire::NowMicros();
            const uint64_t end = start + static_cast<uint64_t>(options.durationMs) * 1000;
            uint64_t lastGrab = 0;
            bool first = true;
            Capture capture;
            while (Wire::NowMicros() < end)
            {
                const uint64_t grabStart = Wire::NowMicros();
                if (lastGrab != 0)
                {
                    loop.push_back(Ms(grabStart - lastGrab));
                }
                lastGrab = grabStart;
                const bool grabbed = screen.Grab(capture);
                const uint64_t grabEnd = Wire::NowMicros();
                ++result.grabs;
                grab.push_back(Ms(grabEnd - grabStart));
                if (grabbed && (first || !capture.dirty.empty()))
                {
                    // The first grab sends the whole desktop, as a session does.
                    std::vector<Wire::Rect> rects = capture.dirty;
                    if (first)
                    {
                        rects = {Wire::Rect{0, 0, capture.width, capture.height}};
                    }
                    first = false;
                    uint64_t packMicros = 0;
                    uint64_t compressMicros = 0;
                    uint64_t frameBytes = 0;
                    double framePixels = 0;
                    for (const Wire::Rect& rect : rects)
                    {
                        if (!RectInside(rect, capture.width, capture.height))
                        {
                            continue;
                        }
                        Wire::Still still;
                        StillTiming timing;
                        EncodeStill(capture, rect, still, &timing);
                        packMicros += timing.packMicros;
                        compressMicros += timing.compressMicros;
                        frameBytes += still.data.size();
                        framePixels += static_cast<double>(rect.width) * rect.height;
                    }
                    ++result.changedFrames;
                    totalBytes += frameBytes;
                    pack.push_back(Ms(packMicros));
                    compress.push_back(Ms(compressMicros));
                    frame.push_back(Ms(grabEnd - grabStart + packMicros + compressMicros));
                    pixels.push_back(framePixels);
                    bytes.push_back(static_cast<double>(frameBytes));
                }
                if (options.frameIntervalMs > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(options.frameIntervalMs));
                }
            }
            result.seconds = Ms(Wire::NowMicros() - start) / 1000.0;
            std::vector<double> grabCopy = grab;
            std::vector<double> compressCopy = compress;
            std::vector<double> frameCopy = frame;
            result.loopMs = Percentile(loop, 0.5);
            result.grabMs = Percentile(grab, 0.5);
            result.grabP99Ms = Percentile(grabCopy, 0.99);
            result.packMs = Percentile(pack, 0.5);
            result.compressMs = Percentile(compress, 0.5);
            result.compressP99Ms = Percentile(compressCopy, 0.99);
            result.frameMs = Percentile(frame, 0.5);
            result.frameP99Ms = Percentile(frameCopy, 0.99);
            result.changedPixels = Percentile(pixels, 0.5);
            result.bytes = Percentile(bytes, 0.5);
            if (result.seconds > 0)
            {
                result.kilobitsPerSecond = static_cast<double>(totalBytes) * 8.0 / 1000.0 / result.seconds;
            }
            return result;
        }

        std::string FormatBench(const BenchResult& r, const std::string& what)
        {
            char text[2048];
            std::snprintf(
                text, sizeof(text),
                "Benchmark of %s: %.1f s, %d screen reads, %d with changes (%.1f a second)\n"
                "  time between reads   %7.2f ms (median)\n"
                "  reading the screen   %7.2f ms (median), %7.2f ms (99th percentile)\n"
                "  packing pixels       %7.2f ms per changed picture (median)\n"
                "  compressing          %7.2f ms (median), %7.2f ms (99th percentile)\n"
                "  read+pack+compress   %7.2f ms (median), %7.2f ms (99th percentile)\n"
                "  changed area         %9.0f pixels per changed picture (median)\n"
                "  compressed size      %9.0f bytes per changed picture (median), %.0f kilobits a second\n",
                what.c_str(), r.seconds, r.grabs, r.changedFrames, r.seconds > 0 ? r.changedFrames / r.seconds : 0.0,
                r.loopMs, r.grabMs, r.grabP99Ms, r.packMs, r.compressMs, r.compressP99Ms, r.frameMs, r.frameP99Ms,
                r.changedPixels, r.bytes, r.kilobitsPerSecond);
            return text;
        }
    } // namespace Server
} // namespace Marconi
