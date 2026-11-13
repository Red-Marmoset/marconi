// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The computer's clipboard as the server sees it: what is on it, and a way to set it

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // What a clipboard holds in the formats Marconi carries. Either part may be empty.
        struct ClipboardContents
        {
            std::string text;          // UTF-8, with "\n" line endings
            std::vector<uint8_t> png;  // an image as a PNG file

            bool IsEmpty() const { return text.empty() && png.empty(); }
            bool operator==(const ClipboardContents& other) const { return text == other.text && png == other.png; }
        };

        // Reads and sets this computer's clipboard. The real one uses the platform (X11 selections, the Windows
        // clipboard); tests use a recording fake. Read and Write may be called from any thread.
        class ClipboardAccess
        {
        public:
            virtual ~ClipboardAccess() = default;

            // The clipboard's current text and image. False if it holds neither (or can't be read).
            virtual bool Read(ClipboardContents& out) = 0;

            // Replaces the clipboard with this content (what is empty is left off). False if it couldn't be set.
            // Changes the platform reports back because of this call may still reach the change handler; the
            // caller recognises them by their content.
            virtual bool Write(const ClipboardContents& contents) = 0;

            // `handler` is called, on any thread, after the clipboard changed (someone copied something). Passing
            // an empty function removes it, and returns only once no call to the old handler is running, so the
            // caller may free what the handler touches straight after.
            virtual void SetChangeHandler(std::function<void()> handler) = 0;
        };

        // The platform's clipboard (X11 on Linux, the Windows clipboard on Windows). Null where there is none yet
        // (macOS) or no display to reach, and the server then runs without clipboard sharing.
        std::unique_ptr<ClipboardAccess> CreateSystemClipboard();
    } // namespace Server
} // namespace Marconi
