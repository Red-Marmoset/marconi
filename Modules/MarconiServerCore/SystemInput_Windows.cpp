// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows input injection with SendInput: the viewer's mouse and keyboard applied to this computer.
//
// NOTE: not yet built or run on Windows from this environment (the cloud build is Linux). The keysym-to-key
// mapping covers the common keys and will likely need adding to once it is tried on a real keyboard.

#include "SystemInput.h"

#include <MarconiCore/Keysyms.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            namespace Keysym = Marconi::Keysym;

            // The Windows virtual-key code for an X11 keysym, or 0 if we don't map it. Printable Latin-1 keysyms
            // equal their character code, so VkKeyScan finds the key; the function keysyms (0xFFxx) are a table.
            WORD VkFromKeysym(uint32_t keysym)
            {
                if (keysym >= 0x20 && keysym <= 0x7E)
                {
                    SHORT scan = VkKeyScanW(static_cast<WCHAR>(keysym));
                    return scan == -1 ? 0 : static_cast<WORD>(scan & 0xFF);
                }
                switch (keysym)
                {
                case Keysym::BackSpace: return VK_BACK;
                case Keysym::Tab: return VK_TAB;
                case Keysym::Return: return VK_RETURN;
                case Keysym::Escape: return VK_ESCAPE;
                case Keysym::Insert: return VK_INSERT;
                case Keysym::Delete: return VK_DELETE;
                case Keysym::Home: return VK_HOME;
                case Keysym::End: return VK_END;
                case Keysym::PageUp: return VK_PRIOR;
                case Keysym::PageDown: return VK_NEXT;
                case Keysym::Left: return VK_LEFT;
                case Keysym::Up: return VK_UP;
                case Keysym::Right: return VK_RIGHT;
                case Keysym::Down: return VK_DOWN;
                case Keysym::Shift_L: return VK_LSHIFT;
                case Keysym::Shift_R: return VK_RSHIFT;
                case Keysym::Control_L: return VK_LCONTROL;
                case Keysym::Control_R: return VK_RCONTROL;
                case Keysym::Alt_L: return VK_LMENU;
                case Keysym::Alt_R: return VK_RMENU;
                case Keysym::Super_L: return VK_LWIN;
                case Keysym::Super_R: return VK_RWIN;
                case Keysym::CapsLock: return VK_CAPITAL;
                case Keysym::Print: return VK_SNAPSHOT;
                default: break;
                }
                if (keysym >= Keysym::F1 && keysym <= Keysym::F1 + 23)
                {
                    return static_cast<WORD>(VK_F1 + (keysym - Keysym::F1));
                }
                return 0;
            }

            bool IsExtendedKey(WORD vk)
            {
                switch (vk)
                {
                case VK_INSERT:
                case VK_DELETE:
                case VK_HOME:
                case VK_END:
                case VK_PRIOR:
                case VK_NEXT:
                case VK_LEFT:
                case VK_UP:
                case VK_RIGHT:
                case VK_DOWN:
                case VK_RCONTROL:
                case VK_RMENU:
                case VK_LWIN:
                case VK_RWIN:
                case VK_SNAPSHOT:
                    return true;
                default:
                    return false;
                }
            }

            class WindowsInput : public InputSink
            {
            public:
                void MovePointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel, int16_t wheelX) override
                {
                    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                    if (width < 2 || height < 2)
                    {
                        return;
                    }
                    // Our coordinates are 0-based from the virtual desktop's top-left, which is what the
                    // VIRTUALDESK absolute range maps onto (0..65535 across the whole virtual desktop).
                    INPUT move = {};
                    move.type = INPUT_MOUSE;
                    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
                    move.mi.dx = static_cast<LONG>(static_cast<long long>(x) * 65535 / (width - 1));
                    move.mi.dy = static_cast<LONG>(static_cast<long long>(y) * 65535 / (height - 1));
                    SendInput(1, &move, sizeof(INPUT));

                    // Buttons: send a down or up for each one whose state changed since last time.
                    static const struct
                    {
                        uint8_t bit;
                        DWORD down;
                        DWORD up;
                    } kButtons[] = {{0x01, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP},
                                    {0x02, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP},
                                    {0x04, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP}};
                    for (const auto& button : kButtons)
                    {
                        bool now = (buttons & button.bit) != 0;
                        bool was = (m_buttons & button.bit) != 0;
                        if (now != was)
                        {
                            INPUT click = {};
                            click.type = INPUT_MOUSE;
                            click.mi.dwFlags = now ? button.down : button.up;
                            SendInput(1, &click, sizeof(INPUT));
                        }
                    }
                    m_buttons = buttons;

                    if (wheel != 0)
                    {
                        INPUT w = {};
                        w.type = INPUT_MOUSE;
                        w.mi.dwFlags = MOUSEEVENTF_WHEEL;
                        w.mi.mouseData = static_cast<DWORD>(wheel);
                        SendInput(1, &w, sizeof(INPUT));
                    }
                    if (wheelX != 0)
                    {
                        INPUT w = {};
                        w.type = INPUT_MOUSE;
                        w.mi.dwFlags = MOUSEEVENTF_HWHEEL;
                        w.mi.mouseData = static_cast<DWORD>(wheelX);
                        SendInput(1, &w, sizeof(INPUT));
                    }
                }

                void SendKey(uint32_t keysym, uint32_t /*scancode*/, bool down) override
                {
                    WORD vk = VkFromKeysym(keysym);
                    if (vk == 0)
                    {
                        return;
                    }
                    INPUT key = {};
                    key.type = INPUT_KEYBOARD;
                    key.ki.wVk = vk;
                    key.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
                    if (IsExtendedKey(vk))
                    {
                        key.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                    }
                    SendInput(1, &key, sizeof(INPUT));
                }

                void TypeText(const std::string& utf8) override
                {
                    // Characters with no plain key: send them as Unicode, which doesn't depend on the layout.
                    std::wstring wide;
                    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
                    if (needed <= 1)
                    {
                        return;
                    }
                    wide.resize(static_cast<size_t>(needed - 1));
                    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], needed);
                    for (wchar_t ch : wide)
                    {
                        for (bool down : {true, false})
                        {
                            INPUT key = {};
                            key.type = INPUT_KEYBOARD;
                            key.ki.wScan = ch;
                            key.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
                            SendInput(1, &key, sizeof(INPUT));
                        }
                    }
                }

            private:
                uint8_t m_buttons = 0;
            };
        } // namespace

        std::unique_ptr<InputSink> CreateSystemInput()
        {
            return std::make_unique<WindowsInput>();
        }
    } // namespace Server
} // namespace Marconi
