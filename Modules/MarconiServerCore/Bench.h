// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiServer --bench: capture and compress the screen for a while with no viewer, and report each stage's time

#pragma once

#include "ScreenSource.h"

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        struct BenchOptions
        {
            int durationMs = 10000;   // how long to run
            int frameIntervalMs = 16; // the pause after each grab, as a session makes (Session's default)
        };

        // What one run measured. Times are in milliseconds: medians, and the 99th percentile where named.
        struct BenchResult
        {
            int grabs = 0;            // screen reads made
            int changedFrames = 0;    // reads where something had changed (what a viewer would be sent)
            double seconds = 0;       // how long the run took
            double loopMs = 0;        // time from one grab to the next (the frame rate's limit)
            double grabMs = 0;        // reading the screen
            double grabP99Ms = 0;
            double packMs = 0;        // copying the changed pixels into the wire's byte order, per changed frame
            double compressMs = 0;    // zlib, per changed frame
            double compressP99Ms = 0;
            double frameMs = 0;       // grab, pack and compress together, per changed frame
            double frameP99Ms = 0;
            double changedPixels = 0; // pixels in the changed areas, per changed frame (median)
            double bytes = 0;         // compressed size per changed frame (median)
            double kilobitsPerSecond = 0;
        };

        // Grabs from `screen` for the given time, compressing what changed exactly as a session would (the same
        // StillEncoder), without sending it anywhere.
        BenchResult RunBench(ScreenSource& screen, const BenchOptions& options);

        // The result as lines of text for the console.
        std::string FormatBench(const BenchResult& result, const std::string& what);
    } // namespace Server
} // namespace Marconi
