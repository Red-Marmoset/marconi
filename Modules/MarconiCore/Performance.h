// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Timing each picture update from capture on the other computer to being shown here, and summing it up for the
// performance readout and the --stats log

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Marconi
{
    // One picture update, as the client saw it. Times are microseconds on this computer's steady clock (see
    // Wire::NowMicros); server times are converted with the clock offset. -1 means not known.
    struct FrameTiming
    {
        uint64_t frameId = 0;
        uint64_t bytes = 0;          // the update's size on the wire, before decryption overhead
        int64_t beganMicros = -1;    // when the update's first message arrived
        int64_t receivedMicros = -1; // when its last message arrived and the picture was applied
        int64_t decodeMicros = -1;   // time spent decompressing and applying it
        int64_t captureMicros = -1;  // when the server captured it (converted to this clock); -1 if unknown
        int64_t sentMicros = -1;     // when the server finished sending it (converted); -1 if unknown
        int64_t compressMicros = -1; // the server's compression time; -1 if unknown
        int64_t shownMicros = -1;    // when a paint first showed it; -1 if not yet (or never, headless)
    };

    // A summary over a recent window. Times are in milliseconds and are medians unless named otherwise; -1 means
    // nothing to report (no frames, or the server doesn't send times).
    struct PerformanceSnapshot
    {
        int frames = 0;              // updates received in the window
        double framesPerSecond = 0;  // updates received a second
        double shownPerSecond = 0;   // paints that showed a new picture, a second
        double kilobitsPerSecond = 0;
        int roundTripMs = -1;
        double serverMs = -1;   // capture to sent, on the server (includes compressing)
        double compressMs = -1; // compressing, on the server
        double networkMs = -1;  // sent by the server to received here
        double decodeMs = -1;   // decompressing and applying here
        double showMs = -1;     // received to shown on screen
        double pipelineMs = -1; // capture to shown (or to received, when nothing paints)
        double pipelineP99Ms = -1;
        // How far the server's clock is ahead of this one, once the two have been compared, so the readout can show
        // the server's clock next to the test pattern's (filming both gives the time to the screen).
        bool clockKnown = false;
        int64_t serverClockOffsetMicros = 0;
    };

    // Collects FrameTimings from the connection thread and paint times from the UI thread. Thread-safe. Keeps
    // the last several seconds only.
    class PerformanceMonitor
    {
    public:
        void Reset();

        // The connection thread: an update has been received and applied.
        void FrameReceived(const FrameTiming& timing);

        // The UI thread: the canvas painted at `nowMicros`, showing every update received so far. Returns the
        // updates this paint showed for the first time (for the --stats log).
        std::vector<FrameTiming> Shown(int64_t nowMicros);

        // A summary of the updates received in the `windowMicros` before `nowMicros`.
        PerformanceSnapshot Summarise(int64_t nowMicros, int64_t windowMicros) const;

    private:
        mutable std::mutex m_mutex;
        std::deque<FrameTiming> m_frames; // oldest first
        std::deque<int64_t> m_paints;     // times of paints that showed something new
    };

    // The value at fraction `q` (0..1) of `values` (sorted in place); -1 for an empty list.
    double Percentile(std::vector<double>& values, double q);
} // namespace Marconi
