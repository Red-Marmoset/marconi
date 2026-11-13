// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning what an AI asks for into input: key names and combinations, typed text, mouse buttons, and screenshot
// coordinates back into desktop ones

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        struct ScreenshotView;

        // The keysym for a key name: "Return", "enter", "tab", "escape", "F5", "left", "page_down", "ctrl", "cmd",
        // ... (case doesn't matter), or a single character (whose case does). 0 if unknown.
        uint32_t KeysymForName(const std::string& name);

        // True if the keysym is a modifier (Shift, Control, Alt, Super, Meta, AltGr).
        bool IsModifier(uint32_t keysym);

        // "ctrl+alt+Delete", "cmd+space", "shift+Tab", "ctrl++": the keys held, in order, then the key pressed.
        // A combination of modifiers alone ("shift") presses that modifier. False, with `error` set, if a name is
        // unknown or the text is empty.
        bool ParseKeyCombination(const std::string& text, std::vector<uint32_t>& held, uint32_t& key,
                                 std::string& error);

        // The keysyms that type `utf8`: one per character, "\n" (or "\r\n") as Return, "\t" as Tab. False, with
        // `error` set, for text that isn't UTF-8 or holds other control characters.
        bool KeysymsForText(const std::string& utf8, std::vector<uint32_t>& keysyms, std::string& error);

        // The VNC button-mask bit for "left", "middle" or "right"; 0 if unknown.
        uint8_t ButtonBit(const std::string& name);

        // The wheel bit for "up", "down", "left" or "right"; 0 if unknown.
        uint8_t WheelBit(const std::string& direction);

        // A point in the last screenshot's pixels as a desktop pixel (the middle of the area that picture pixel
        // covers), clamped to the desktop. Without a screenshot, the point is taken as desktop pixels already.
        // False, with `error` set, if the point is outside the screenshot.
        bool ToDesktop(const ScreenshotView& view, int desktopWidth, int desktopHeight, int x, int y, int& outX,
                       int& outY, std::string& error);
    } // namespace Mcp
} // namespace Marconi
