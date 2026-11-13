// Copyright (c) 2026 Red Marmoset Studios Ltd.
// macOS input injection with Core Graphics events (CGEventPost): the viewer's mouse and keyboard applied to this Mac.
//
// NOTE: written without a Mac to build on; NOT yet compiled or run on macOS. SERVER_PLAN.md (Phase 8) lists what
// has to be checked on a Mac.
//
// Needs the Accessibility permission (System Settings > Privacy & Security > Accessibility); without it macOS drops
// the events silently, so it is checked first and explained in plain words.
//
// Decisions:
//  - Pointer positions arrive in the served desktop's points (see SystemScreen_Mac.cpp) and are moved to the Mac's
//    global points by adding the desktop's origin, so any number of displays, in any arrangement, work and Retina
//    needs no extra scaling.
//  - Keys. macOS keys are physical (virtual key codes), not X keysyms, so a keysym is turned into either a key code
//    or a typed character:
//      * Special keys (Return, arrows, F-keys, keypad ...) and modifiers go by key code from a fixed table.
//      * Letters, digits and symbols are typed as the character itself (CGEventKeyboardSetUnicodeString), so the
//        right character appears whatever layout the Mac has. The US key position for the character is sent as the
//        key code too, for programs that look at it.
//      * While Control, Option or Command is held, they go by US key position only (no text), so shortcuts like
//        Command-C behave like the real keys, on the layouts where the letters keep their place.
//      * Modifier mapping is literal: Shift to Shift, Control to Control, Alt to Option, Windows/Super to Command.
//        (Mapping Control to Command, so a Windows viewer's Ctrl+C copies on the Mac, is a choice for the owner.)
//  - Hardware scancodes (the `scancode` argument) are ignored for now.
//  - Wheel: 120 units is one notch, which scrolls three lines; smaller steps add up.

#include "SystemInput.h"

#include "MacDisplays.h"

#include <MarconiCore/Keysyms.h>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr const char* kAccessibilityText =
                "Marconi Server needs your permission to control this Mac, so viewers can use the mouse and "
                "keyboard. Open System Settings, go to Privacy & Security, then Accessibility, and turn on Marconi "
                "Server. Until then viewers can see this Mac but not control it.";

            // macOS virtual key codes (the kVK_ values of Carbon's Events.h, written out so Carbon isn't needed).
            enum : CGKeyCode
            {
                kVK_Return = 36,
                kVK_Tab = 48,
                kVK_Space = 49,
                kVK_Delete = 51, // the Backspace key
                kVK_Escape = 53,
                kVK_Command = 55,
                kVK_Shift = 56,
                kVK_CapsLock = 57,
                kVK_Option = 58,
                kVK_Control = 59,
                kVK_RightCommand = 54,
                kVK_RightShift = 60,
                kVK_RightOption = 61,
                kVK_RightControl = 62,
                kVK_ContextualMenu = 110,
                kVK_Help = 114, // Insert on a PC keyboard
                kVK_Home = 115,
                kVK_PageUp = 116,
                kVK_ForwardDelete = 117,
                kVK_End = 119,
                kVK_PageDown = 121,
                kVK_LeftArrow = 123,
                kVK_RightArrow = 124,
                kVK_DownArrow = 125,
                kVK_UpArrow = 126,
                kVK_ANSI_KeypadDecimal = 65,
                kVK_ANSI_KeypadMultiply = 67,
                kVK_ANSI_KeypadPlus = 69,
                kVK_ANSI_KeypadDivide = 75,
                kVK_ANSI_KeypadEnter = 76,
                kVK_ANSI_KeypadMinus = 78,
                kVK_ANSI_KeypadEquals = 81,
            };

            constexpr CGKeyCode kNoKey = 0xFFFF;

            // The US-layout key for a character, for shortcuts and for programs that read key codes. Shifted
            // symbols and capitals map to the key they share with their plain form (the viewer sends Shift itself).
            CGKeyCode AsciiKeyCode(uint32_t c)
            {
                static const struct
                {
                    char character;
                    CGKeyCode code;
                } kKeys[] = {
                    {'a', 0},  {'s', 1},  {'d', 2},  {'f', 3},  {'h', 4},  {'g', 5},  {'z', 6},  {'x', 7},  {'c', 8},
                    {'v', 9},  {'b', 11}, {'q', 12}, {'w', 13}, {'e', 14}, {'r', 15}, {'y', 16}, {'t', 17}, {'1', 18},
                    {'2', 19}, {'3', 20}, {'4', 21}, {'6', 22}, {'5', 23}, {'=', 24}, {'9', 25}, {'7', 26}, {'-', 27},
                    {'8', 28}, {'0', 29}, {']', 30}, {'o', 31}, {'u', 32}, {'[', 33}, {'i', 34}, {'p', 35}, {'l', 37},
                    {'j', 38}, {'\'', 39}, {'k', 40}, {';', 41}, {'\\', 42}, {',', 43}, {'/', 44}, {'n', 45}, {'m', 46},
                    {'.', 47}, {'`', 50}, {' ', kVK_Space}};
                if (c >= 'A' && c <= 'Z')
                {
                    c = c - 'A' + 'a';
                }
                else
                {
                    static const char kShifted[] = "!@#$%^&*()_+{}|:\"<>?~";
                    static const char kPlain[] = "1234567890-=[]\\;',./`";
                    for (size_t i = 0; kShifted[i] != 0; ++i)
                    {
                        if (c == static_cast<uint32_t>(kShifted[i]))
                        {
                            c = static_cast<uint32_t>(kPlain[i]);
                            break;
                        }
                    }
                }
                for (const auto& key : kKeys)
                {
                    if (static_cast<uint32_t>(key.character) == c)
                    {
                        return key.code;
                    }
                }
                return kNoKey;
            }

            // Keys that have no character: by key code. kNoKey when the keysym isn't one of them.
            CGKeyCode SpecialKeyCode(uint32_t keysym)
            {
                using namespace Marconi::Keysym;
                switch (keysym)
                {
                case BackSpace: return kVK_Delete;
                case Tab: return kVK_Tab;
                case 0xFE20: return kVK_Tab; // ISO_Left_Tab (Shift+Tab)
                case Return: return kVK_Return;
                case 0xFF0A: return kVK_Return; // Linefeed
                case Escape: return kVK_Escape;
                case Delete: return kVK_ForwardDelete;
                case Home: case KP_Home: return kVK_Home;
                case End: case KP_End: return kVK_End;
                case PageUp: case KP_PageUp: return kVK_PageUp;
                case PageDown: case KP_PageDown: return kVK_PageDown;
                case Left: case KP_Left: return kVK_LeftArrow;
                case Right: case KP_Right: return kVK_RightArrow;
                case Up: case KP_Up: return kVK_UpArrow;
                case Down: case KP_Down: return kVK_DownArrow;
                case KP_Delete: return kVK_ForwardDelete;
                case Insert: case KP_Insert: case Help: return kVK_Help;
                case Menu: return kVK_ContextualMenu;
                case KP_Enter: return kVK_ANSI_KeypadEnter;
                case KP_Multiply: return kVK_ANSI_KeypadMultiply;
                case KP_Add: return kVK_ANSI_KeypadPlus;
                case KP_Subtract: return kVK_ANSI_KeypadMinus;
                case KP_Decimal: return kVK_ANSI_KeypadDecimal;
                case KP_Divide: return kVK_ANSI_KeypadDivide;
                case KP_Equal: return kVK_ANSI_KeypadEquals;
                default: break;
                }
                if (keysym >= KP_0 && keysym <= KP_0 + 9)
                {
                    static const CGKeyCode kKeypad[10] = {82, 83, 84, 85, 86, 87, 88, 89, 91, 92};
                    return kKeypad[keysym - KP_0];
                }
                if (keysym >= F1 && keysym <= F1 + 19)
                {
                    static const CGKeyCode kFunction[20] = {122, 120, 99,  118, 96,  97,  98,  100, 101, 109,
                                                            103, 111, 105, 107, 113, 106, 64,  79,  80,  90};
                    return kFunction[keysym - F1];
                }
                return kNoKey;
            }

            struct ModifierKey
            {
                CGKeyCode code;
                CGEventFlags flag;
            };

            // The modifier a keysym stands for, if it is one. Both Shift keys (and so on) share a flag.
            bool ModifierFor(uint32_t keysym, ModifierKey& out)
            {
                using namespace Marconi::Keysym;
                switch (keysym)
                {
                case Shift_L: out = {kVK_Shift, kCGEventFlagMaskShift}; return true;
                case Shift_R: out = {kVK_RightShift, kCGEventFlagMaskShift}; return true;
                case Control_L: out = {kVK_Control, kCGEventFlagMaskControl}; return true;
                case Control_R: out = {kVK_RightControl, kCGEventFlagMaskControl}; return true;
                case Alt_L: case Meta_L: out = {kVK_Option, kCGEventFlagMaskAlternate}; return true;
                case Alt_R: case Meta_R: case ISO_Level3_Shift:
                    out = {kVK_RightOption, kCGEventFlagMaskAlternate};
                    return true;
                case Super_L: out = {kVK_Command, kCGEventFlagMaskCommand}; return true;
                case Super_R: out = {kVK_RightCommand, kCGEventFlagMaskCommand}; return true;
                default: return false;
                }
            }

            // The Unicode character a keysym stands for (Latin-1 as is, or the 0x01000000 range), or 0.
            uint32_t CodepointFor(uint32_t keysym)
            {
                if ((keysym >= 0x20 && keysym <= 0x7E) || (keysym >= 0xA0 && keysym <= 0xFF))
                {
                    return keysym;
                }
                if ((keysym & 0xFF000000u) == 0x01000000u)
                {
                    return keysym & 0x00FFFFFFu;
                }
                return 0;
            }

            void AppendUtf16(std::u16string& out, uint32_t codepoint)
            {
                if (codepoint >= 0x10000)
                {
                    codepoint -= 0x10000;
                    out.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
                    out.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
                }
                else
                {
                    out.push_back(static_cast<char16_t>(codepoint));
                }
            }

            // The mouse buttons the wire carries (bit 0 left, 1 middle, 2 right) and their Core Graphics events.
            struct ButtonInfo
            {
                uint8_t bit;
                CGMouseButton button;
                CGEventType down;
                CGEventType up;
                CGEventType drag;
            };

            const ButtonInfo kButtons[3] = {
                {0x01, kCGMouseButtonLeft, kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGEventLeftMouseDragged},
                {0x04, kCGMouseButtonRight, kCGEventRightMouseDown, kCGEventRightMouseUp, kCGEventRightMouseDragged},
                {0x02, kCGMouseButtonCenter, kCGEventOtherMouseDown, kCGEventOtherMouseUp,
                 kCGEventOtherMouseDragged}};

            class MacInput : public InputSink
            {
            public:
                ~MacInput() override
                {
                    ReleaseAll();
                    if (m_source != nullptr)
                    {
                        CFRelease(m_source);
                    }
                }

                bool Open(std::string& error)
                {
                    // The prompt option makes macOS show its own "allow in Accessibility" dialog the first time.
                    const void* keys[] = {kAXTrustedCheckOptionPrompt};
                    const void* values[] = {kCFBooleanTrue};
                    CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
                    const bool trusted = AXIsProcessTrustedWithOptions(options);
                    if (options != nullptr)
                    {
                        CFRelease(options);
                    }
                    if (!trusted)
                    {
                        error = kAccessibilityText;
                        return false;
                    }
                    m_source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
                    if (m_source == nullptr)
                    {
                        error = "Could not set up mouse and keyboard control on this Mac.";
                        return false;
                    }
                    return true;
                }

                void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const CGPoint point = ToGlobal(x, y);

                    // Move first (a drag if a button is already down), then the button changes at the new place.
                    if (!m_havePosition || point.x != m_position.x || point.y != m_position.y)
                    {
                        PostMouse(MoveType(), point, MoveButton(), 0);
                        m_position = point;
                        m_havePosition = true;
                    }

                    for (const auto& button : kButtons)
                    {
                        const bool now = (buttons & button.bit) != 0;
                        const bool was = (m_buttons & button.bit) != 0;
                        if (now == was)
                        {
                            continue;
                        }
                        int clicks = 1;
                        if (now)
                        {
                            clicks = NextClickCount(button.bit, point);
                            m_clicks[button.bit] = clicks;
                        }
                        else
                        {
                            clicks = m_clicks[button.bit] > 0 ? m_clicks[button.bit] : 1;
                        }
                        PostMouse(now ? button.down : button.up, point, button.button, clicks);
                    }
                    m_buttons = buttons;

                    Scroll(wheel, wheelX);
                }

                void SendKey(uint32_t keysym, uint32_t /*scancode*/, bool down) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);

                    ModifierKey modifier;
                    if (ModifierFor(keysym, modifier))
                    {
                        SendModifier(modifier, down);
                        return;
                    }
                    if (keysym == Keysym::CapsLock)
                    {
                        // The Caps Lock key toggles on each press; send the pair on the press only.
                        if (down)
                        {
                            m_capsLock = !m_capsLock;
                            PostKey(kVK_CapsLock, true, EffectiveFlags(), nullptr);
                            PostKey(kVK_CapsLock, false, EffectiveFlags(), nullptr);
                        }
                        return;
                    }

                    auto held = m_down.find(keysym);
                    if (!down)
                    {
                        // Release exactly what the press sent, whatever the modifiers are doing by now.
                        if (held != m_down.end())
                        {
                            PostKey(held->second.code, false, EffectiveFlags(),
                                    held->second.text.empty() ? nullptr : &held->second.text);
                            m_down.erase(held);
                        }
                        return;
                    }

                    const bool repeat = held != m_down.end();
                    Press press;
                    if (repeat)
                    {
                        press = held->second;
                    }
                    else if (!ChoosePress(keysym, press))
                    {
                        return; // nothing on a Mac keyboard makes this key
                    }
                    PostKey(press.code, true, EffectiveFlags(), press.text.empty() ? nullptr : &press.text, repeat);
                    m_down[keysym] = press;
                }

                void TypeText(const std::string& utf8) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    size_t i = 0;
                    while (i < utf8.size())
                    {
                        // A few characters at a time: one keystroke can carry up to 20 UTF-16 units.
                        std::u16string chunk;
                        while (i < utf8.size() && chunk.size() < 18)
                        {
                            uint32_t codepoint = 0;
                            const unsigned char lead = static_cast<unsigned char>(utf8[i]);
                            const int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
                            codepoint = length == 1 ? lead : lead & (0xFF >> (length + 1));
                            for (int k = 1; k < length && i + k < utf8.size(); ++k)
                            {
                                codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
                            }
                            i += length;
                            if (codepoint >= 0x20 || codepoint == '\n' || codepoint == '\t')
                            {
                                AppendUtf16(chunk, codepoint == '\n' ? '\r' : codepoint);
                            }
                        }
                        if (!chunk.empty())
                        {
                            // Typed text ignores held modifiers (flags 0): it is characters, not shortcuts.
                            PostKey(0, true, 0, &chunk);
                            PostKey(0, false, 0, &chunk);
                        }
                    }
                }

            private:
                struct Press
                {
                    CGKeyCode code = 0;
                    std::u16string text; // empty: send the key code alone
                };

                // The served desktop's (x,y) as a point in the Mac's global space, kept inside the displays.
                CGPoint ToGlobal(int32_t x, int32_t y)
                {
                    auto displays = ListMacDisplays();
                    if (displays.empty())
                    {
                        return CGPointMake(x, y);
                    }
                    const CGRect desktop = MacDesktopBounds(displays);
                    double globalX = desktop.origin.x + x;
                    double globalY = desktop.origin.y + y;
                    globalX = std::fmax(desktop.origin.x, std::fmin(globalX, desktop.origin.x + desktop.size.width - 1));
                    globalY = std::fmax(desktop.origin.y, std::fmin(globalY, desktop.origin.y + desktop.size.height - 1));
                    return CGPointMake(globalX, globalY);
                }

                // While a button is held, movement is a drag of that button.
                CGEventType MoveType() const
                {
                    for (const auto& button : kButtons)
                    {
                        if ((m_buttons & button.bit) != 0)
                        {
                            return button.drag;
                        }
                    }
                    return kCGEventMouseMoved;
                }

                CGMouseButton MoveButton() const
                {
                    for (const auto& button : kButtons)
                    {
                        if ((m_buttons & button.bit) != 0)
                        {
                            return button.button;
                        }
                    }
                    return kCGMouseButtonLeft;
                }

                // 1 for a single click, 2 for a double click, 3 for a triple: presses of the same button close
                // together in time and place.
                int NextClickCount(uint8_t bit, CGPoint point)
                {
                    const auto now = std::chrono::steady_clock::now();
                    int count = 1;
                    if (bit == m_lastClickBit && now - m_lastClickTime < std::chrono::milliseconds(500) &&
                        std::fabs(point.x - m_lastClickPoint.x) <= 4 && std::fabs(point.y - m_lastClickPoint.y) <= 4)
                    {
                        count = m_lastClickCount % 3 + 1;
                    }
                    m_lastClickBit = bit;
                    m_lastClickTime = now;
                    m_lastClickPoint = point;
                    m_lastClickCount = count;
                    return count;
                }

                void PostMouse(CGEventType type, CGPoint point, CGMouseButton button, int clickCount)
                {
                    CGEventRef event = CGEventCreateMouseEvent(m_source, type, point, button);
                    if (event == nullptr)
                    {
                        return;
                    }
                    if (clickCount > 0)
                    {
                        CGEventSetIntegerValueField(event, kCGMouseEventClickState, clickCount);
                    }
                    CGEventSetFlags(event, EffectiveFlags());
                    CGEventPost(kCGHIDEventTap, event);
                    CFRelease(event);
                }

                // 120 wheel units make a notch, and a notch is three lines. Small steps add up until they make one.
                void Scroll(int16_t wheel, int16_t wheelX)
                {
                    m_wheelRemainder += wheel;
                    m_wheelXRemainder += wheelX;
                    const int notches = m_wheelRemainder / 120;
                    const int notchesX = m_wheelXRemainder / 120;
                    m_wheelRemainder -= notches * 120;
                    m_wheelXRemainder -= notchesX * 120;
                    if (notches == 0 && notchesX == 0)
                    {
                        return;
                    }
                    constexpr int kLinesPerNotch = 3;
                    // Positive vertical scrolls up (as on Windows and X11). On a Mac a positive horizontal value
                    // scrolls left, the opposite of the wire's (positive = right), so it is negated.
                    CGEventRef event = CGEventCreateScrollWheelEvent(m_source, kCGScrollEventUnitLine, 2,
                                                                     notches * kLinesPerNotch,
                                                                     -notchesX * kLinesPerNotch);
                    if (event == nullptr)
                    {
                        return;
                    }
                    CGEventSetFlags(event, EffectiveFlags());
                    CGEventPost(kCGHIDEventTap, event);
                    CFRelease(event);
                }

                CGEventFlags EffectiveFlags() const
                {
                    CGEventFlags flags = 0;
                    for (const auto& entry : m_modifiersDown)
                    {
                        flags |= entry.second;
                    }
                    if (m_capsLock)
                    {
                        flags |= kCGEventFlagMaskAlphaShift;
                    }
                    return flags;
                }

                void SendModifier(const ModifierKey& modifier, bool down)
                {
                    if (down)
                    {
                        m_modifiersDown[modifier.code] = modifier.flag;
                    }
                    else
                    {
                        m_modifiersDown.erase(modifier.code);
                    }
                    PostKey(modifier.code, down, EffectiveFlags(), nullptr);
                }

                // Decides how to send a keysym: by key code, as a typed character, or both.
                bool ChoosePress(uint32_t keysym, Press& press)
                {
                    const CGKeyCode special = SpecialKeyCode(keysym);
                    if (special != kNoKey)
                    {
                        press.code = special;
                        return true;
                    }
                    const uint32_t codepoint = CodepointFor(keysym);
                    if (codepoint == 0)
                    {
                        return false;
                    }
                    const CGKeyCode ascii = AsciiKeyCode(codepoint);
                    const CGEventFlags flags = EffectiveFlags();
                    const bool shortcut = (flags & (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate |
                                                    kCGEventFlagMaskCommand)) != 0;
                    if (shortcut && ascii != kNoKey)
                    {
                        press.code = ascii; // by key position, so Command-C is the real shortcut
                        return true;
                    }
                    press.code = ascii != kNoKey ? ascii : 0;
                    AppendUtf16(press.text, codepoint);
                    return true;
                }

                void PostKey(CGKeyCode code, bool down, CGEventFlags flags, const std::u16string* text,
                             bool repeat = false)
                {
                    CGEventRef event = CGEventCreateKeyboardEvent(m_source, code, down);
                    if (event == nullptr)
                    {
                        return;
                    }
                    CGEventSetFlags(event, flags);
                    if (text != nullptr && !text->empty())
                    {
                        CGEventKeyboardSetUnicodeString(event, static_cast<UniCharCount>(text->size()),
                                                        reinterpret_cast<const UniChar*>(text->data()));
                    }
                    if (repeat)
                    {
                        CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, 1);
                    }
                    CGEventPost(kCGHIDEventTap, event);
                    CFRelease(event);
                }

                // Lets go of whatever is still held, so a viewer that vanished leaves no key or button stuck.
                void ReleaseAll()
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_source == nullptr)
                    {
                        return;
                    }
                    for (const auto& entry : m_down)
                    {
                        PostKey(entry.second.code, false, 0, nullptr);
                    }
                    m_down.clear();
                    for (const auto& entry : m_modifiersDown)
                    {
                        PostKey(entry.first, false, 0, nullptr);
                    }
                    m_modifiersDown.clear();
                    for (const auto& button : kButtons)
                    {
                        if ((m_buttons & button.bit) != 0)
                        {
                            PostMouse(button.up, m_position, button.button, 1);
                        }
                    }
                    m_buttons = 0;
                }

                CGEventSourceRef m_source = nullptr;
                std::mutex m_mutex;

                uint8_t m_buttons = 0;
                bool m_havePosition = false;
                CGPoint m_position = {0, 0};
                int m_clicks[8] = {};
                uint8_t m_lastClickBit = 0;
                std::chrono::steady_clock::time_point m_lastClickTime;
                CGPoint m_lastClickPoint = {0, 0};
                int m_lastClickCount = 0;
                int m_wheelRemainder = 0;
                int m_wheelXRemainder = 0;

                std::map<CGKeyCode, CGEventFlags> m_modifiersDown; // held modifier keys and the flag each sets
                std::map<uint32_t, Press> m_down;                  // keysym -> what its press sent
                bool m_capsLock = false;
            };
        } // namespace

        std::unique_ptr<InputSink> CreateSystemInput(std::string& error)
        {
            auto input = std::make_unique<MacInput>();
            if (!input->Open(error))
            {
                return nullptr; // not allowed yet: the caller falls back to logging the input
            }
            return input;
        }
    } // namespace Server
} // namespace Marconi
