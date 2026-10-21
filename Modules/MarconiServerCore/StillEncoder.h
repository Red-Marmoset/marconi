// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning a rectangle of a capture into a lossless Still message's contents, timed, for the session and --bench

#pragma once

#include "ScreenSource.h"

#include <MarconiCore/Wire/WireMessages.h>

#include <cstdint>

namespace Marconi
{
    namespace Server
    {
        // How long the last EncodeStill spent on each part, in microseconds.
        struct StillTiming
        {
            uint64_t packMicros = 0;     // copying the pixels into the wire's byte order
            uint64_t compressMicros = 0; // zlib
        };

        // True if `rect` is non-empty and lies inside a width x height picture.
        bool RectInside(const Wire::Rect& rect, int32_t width, int32_t height);

        // Fills `still` with `rect` of `capture` (which must be inside it): zlib-compressed unless zlib can't beat
        // the raw pixels (tiny or already-random rectangles). A screen is mostly flat colour and text, so this
        // usually sends far less. `timing`, if given, receives the time each part took.
        void EncodeStill(const Capture& capture, const Wire::Rect& rect, Wire::Still& still,
                         StillTiming* timing = nullptr);
    } // namespace Server
} // namespace Marconi
