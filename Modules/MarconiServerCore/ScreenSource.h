// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the server needs from the computer's screen: its displays, the current picture, and what changed since last

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // One capture of the whole desktop: its size, the pixels (0x00RRGGBB, one uint32 per pixel, row by row),
        // and the areas that changed since the previous capture. On the first capture `dirty` is the whole desktop.
        struct Capture
        {
            int32_t width = 0;
            int32_t height = 0;
            std::vector<uint32_t> pixels;   // width*height
            std::vector<Wire::Rect> dirty;  // changed rectangles; empty means nothing changed

            const uint32_t* Row(int y) const { return pixels.data() + static_cast<size_t>(y) * width; }
        };

        // The pointer, for drawing it on the client instead of baking it into the picture.
        struct Cursor
        {
            int32_t width = 0;
            int32_t height = 0;
            int32_t hotspotX = 0;
            int32_t hotspotY = 0;
            std::vector<uint32_t> argb; // width*height, alpha 0 or 255; empty = hidden
        };

        // The computer's screen, as the session reads it. The real one (Desktop Duplication on Windows, and so on)
        // and a test pattern both implement this; tests use a scripted fake. Called from the session's frame
        // thread only, so implementations needn't be thread-safe.
        class ScreenSource
        {
        public:
            virtual ~ScreenSource() = default;

            // The displays and their layout, for Welcome and the Displays message.
            virtual std::vector<Wire::Display> GetDisplays() = 0;

            // Fills `out` with the current desktop and the areas changed since the previous Grab (the whole desktop
            // the first time). Returns false if the screen can't be read right now (the session waits and retries).
            virtual bool Grab(Capture& out) = 0;

            // The current pointer, if the source tracks it; the default is no cursor (the client shows its own).
            virtual bool GetCursor(Cursor& /*out*/) { return false; }
        };
    } // namespace Server
} // namespace Marconi
