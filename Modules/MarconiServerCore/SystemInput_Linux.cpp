// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux input injection on X11 with the XTest extension: the viewer's mouse and keyboard applied to this computer.
// The wire's keysyms are X keysyms already, so keys map straight onto the keyboard's own layout.

#include "SystemInput.h"
#include "SystemLock.h"

#include "X11Support.h"

#include <MarconiCore/Keysyms.h>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cstdint>
#include <mutex>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            class LinuxInput : public InputSink
            {
            public:
                ~LinuxInput() override
                {
                    if (m_display != nullptr)
                    {
                        XCloseDisplay(m_display);
                    }
                }

                bool Open(std::string& error)
                {
                    if (!X11OpenDisplay(m_display, error))
                    {
                        return false;
                    }
                    int eventBase = 0;
                    int errorBase = 0;
                    int major = 0;
                    int minor = 0;
                    if (XTestQueryExtension(m_display, &eventBase, &errorBase, &major, &minor) == 0)
                    {
                        error = "This desktop doesn't allow other programs to press keys and move the mouse (its "
                                "XTEST extension is turned off), so viewers can see it but not control it.";
                        return false;
                    }
                    return true;
                }

                void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    XTestFakeMotionEvent(m_display, -1, x, y, CurrentTime);

                    // X buttons: 1 left, 2 middle, 3 right; the wheel is buttons 4/5 (up/down) and 6/7 (left/right).
                    static const struct
                    {
                        uint8_t bit;
                        unsigned x11;
                    } kButtons[] = {{0x01, 1}, {0x02, 2}, {0x04, 3}};
                    for (const auto& button : kButtons)
                    {
                        bool now = (buttons & button.bit) != 0;
                        bool was = (m_buttons & button.bit) != 0;
                        if (now != was)
                        {
                            XTestFakeButtonEvent(m_display, button.x11, now ? True : False, CurrentTime);
                        }
                    }
                    m_buttons = buttons;

                    Scroll(wheel, 4, 5);
                    Scroll(wheelX, 7, 6);
                    XFlush(m_display);
                }

                void SendKey(uint32_t keysym, uint32_t /*scancode*/, bool down) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    KeyCode code = XKeysymToKeycode(m_display, static_cast<KeySym>(keysym));
                    if (code == 0)
                    {
                        // No key makes this character (an accent or another script's letter): type it by borrowing
                        // a spare key, once, on the press. The release has nothing left to do.
                        if (down)
                        {
                            TypeKeysym(keysym);
                            XFlush(m_display);
                        }
                        return;
                    }
                    XTestFakeKeyEvent(m_display, code, down ? True : False, CurrentTime);
                    XFlush(m_display);
                }

                bool LockWorkstation() override { return LockThisComputer(); }

                void TypeText(const std::string& utf8) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    size_t i = 0;
                    while (i < utf8.size())
                    {
                        uint32_t codepoint = 0;
                        unsigned char lead = static_cast<unsigned char>(utf8[i]);
                        int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
                        codepoint = length == 1 ? lead : lead & (0xFF >> (length + 1));
                        for (int k = 1; k < length && i + k < utf8.size(); ++k)
                        {
                            codepoint = (codepoint << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
                        }
                        i += length;
                        uint32_t keysym = Marconi::Keysym::FromUnicode(codepoint);
                        if (keysym != 0)
                        {
                            TypeKeysym(keysym);
                        }
                    }
                    XFlush(m_display);
                }

            private:
                void Scroll(int16_t delta, unsigned positiveButton, unsigned negativeButton)
                {
                    if (delta == 0)
                    {
                        return;
                    }
                    int notches = (delta < 0 ? -delta : delta) / 120;
                    if (notches == 0)
                    {
                        notches = 1;
                    }
                    unsigned button = delta > 0 ? positiveButton : negativeButton;
                    for (int n = 0; n < notches; ++n)
                    {
                        XTestFakeButtonEvent(m_display, button, True, CurrentTime);
                        XTestFakeButtonEvent(m_display, button, False, CurrentTime);
                    }
                }

                // Types a character the keyboard has no key for by borrowing a spare key code for a moment.
                void TypeKeysym(uint32_t keysym)
                {
                    KeyCode code = XKeysymToKeycode(m_display, static_cast<KeySym>(keysym));
                    bool borrowed = false;
                    if (code == 0)
                    {
                        int minimum = 0;
                        int maximum = 0;
                        XDisplayKeycodes(m_display, &minimum, &maximum);
                        code = static_cast<KeyCode>(maximum);
                        KeySym symbols[1] = {static_cast<KeySym>(keysym)};
                        XChangeKeyboardMapping(m_display, code, 1, symbols, 1);
                        XSync(m_display, False);
                        borrowed = true;
                    }
                    XTestFakeKeyEvent(m_display, code, True, CurrentTime);
                    XTestFakeKeyEvent(m_display, code, False, CurrentTime);
                    XSync(m_display, False);
                    if (borrowed)
                    {
                        KeySym none[1] = {NoSymbol};
                        XChangeKeyboardMapping(m_display, code, 1, none, 1);
                        XSync(m_display, False);
                    }
                }

                Display* m_display = nullptr;
                std::mutex m_mutex;
                uint8_t m_buttons = 0;
            };
        } // namespace

        std::unique_ptr<InputSink> CreateSystemInput(std::string& error)
        {
            auto input = std::make_unique<LinuxInput>();
            if (!input->Open(error))
            {
                return nullptr; // no X display or no XTest: the caller falls back to logging the input
            }
            return input;
        }
    } // namespace Server
} // namespace Marconi
