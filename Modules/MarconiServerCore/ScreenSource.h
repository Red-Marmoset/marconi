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

            // The pointer's image, only when it is new: true the first time and again whenever the shape changed
            // since the last call that returned true; false means "same as before" (or the source has no cursor,
            // and the client shows its own). An empty `argb` means the pointer is hidden. Called every frame
            // iteration, so it must be cheap when nothing changed.
            virtual bool GetCursor(Cursor& /*out*/) { return false; }

            // Where the pointer is, in desktop coordinates. False if the source doesn't know.
            virtual bool GetCursorPosition(int32_t& /*x*/, int32_t& /*y*/) { return false; }

            // Whether the source can change a display's size (Capability::ResizeDisplay is granted only if so).
            virtual bool CanSetDisplayMode() { return false; }

            // Changes the display's size to the listed mode closest to width x height, never to one the display
            // doesn't list. The result says what happened; the new layout shows up in the next GetDisplays.
            virtual Wire::DisplayModeResult::Status SetDisplayMode(uint32_t /*displayId*/, int32_t /*width*/,
                                                                   int32_t /*height*/)
            {
                return Wire::DisplayModeResult::Status::NotAllowed;
            }

            // Puts every display the source changed back to how it was. Called when the viewer leaves; safe to call
            // when nothing was changed.
            virtual void RestoreDisplayModes() {}
        };
    } // namespace Server
} // namespace Marconi
