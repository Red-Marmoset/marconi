// Copyright (c) 2026 Red Marmoset Studios Ltd.
// System shortcut capture on Windows: a low-level keyboard hook swallows the shortcuts and sends them instead
//
// The hook runs on the thread that installed it (the UI thread), inside its message loop, so `send` is called
// there. Ctrl+Alt+Del can't be caught by any hook; the Send Keys menu covers it.

#include "SystemKeys.h"

#include <MarconiCore/Keysyms.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <map>
#include <string>

namespace MarconiUI
{
    namespace
    {
        namespace Keysym = Marconi::Keysym;

        class WindowsSystemKeyCapture : public SystemKeyCapture
        {
        public:
            explicit WindowsSystemKeyCapture(SendKey send) : m_send(std::move(send)) {}

            ~WindowsSystemKeyCapture() override { Stop(); }

            bool Start(std::string& problem) override
            {
                if (m_hook != nullptr)
                {
                    return true;
                }
                if (s_instance != nullptr)
                {
                    problem = "another window is already sending system shortcuts";
                    return false;
                }
                m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &WindowsSystemKeyCapture::Hook, GetModuleHandleW(nullptr), 0);
                if (m_hook == nullptr)
                {
                    problem = "Windows refused the keyboard hook (error " + std::to_string(GetLastError()) + ")";
                    return false;
                }
                s_instance = this;
                return true;
            }

            void Stop() override
            {
                if (m_hook != nullptr)
                {
                    UnhookWindowsHookEx(m_hook);
                    m_hook = nullptr;
                    s_instance = nullptr;
                    ReleaseHeld();
                }
            }

            bool IsActive() const override { return m_hook != nullptr; }

        private:
            static bool IsDown(int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; }

            // The keysym to send for a key the system would otherwise act on, or 0 to let it through.
            static uint32_t CapturedKeysym(const KBDLLHOOKSTRUCT& key)
            {
                bool alt = (key.flags & LLKHF_ALTDOWN) != 0;
                bool ctrl = IsDown(VK_CONTROL);
                bool win = IsDown(VK_LWIN) || IsDown(VK_RWIN);
                switch (key.vkCode)
                {
                case VK_LWIN:
                    return Keysym::Super_L;
                case VK_RWIN:
                    return Keysym::Super_R;
                case VK_TAB:
                    return alt ? Keysym::Tab : 0; // Alt+Tab
                case VK_ESCAPE:
                    return (alt || ctrl) ? Keysym::Escape : 0; // Alt+Esc, Ctrl+Esc
                case VK_F4:
                    return alt ? Keysym::F1 + 3 : 0; // Alt+F4 would close this window
                case VK_SNAPSHOT:
                    return Keysym::Print;
                default:
                    break;
                }
                if (win)
                {
                    // Anything with the Windows key held: letters, digits and a few more Windows uses.
                    if (key.vkCode >= 'A' && key.vkCode <= 'Z')
                    {
                        return 'a' + (key.vkCode - 'A');
                    }
                    if (key.vkCode >= '0' && key.vkCode <= '9')
                    {
                        return key.vkCode;
                    }
                    switch (key.vkCode)
                    {
                    case VK_LEFT: return Keysym::Left;
                    case VK_RIGHT: return Keysym::Right;
                    case VK_UP: return Keysym::Up;
                    case VK_DOWN: return Keysym::Down;
                    case VK_SPACE: return ' ';
                    case VK_RETURN: return Keysym::Return;
                    case VK_OEM_COMMA: return ',';
                    case VK_OEM_PERIOD: return '.';
                    case VK_OEM_MINUS: return '-';
                    case VK_OEM_PLUS: return '=';
                    default: break;
                    }
                }
                return 0;
            }

            static LRESULT CALLBACK Hook(int code, WPARAM wParam, LPARAM lParam)
            {
                if (code == HC_ACTION && s_instance != nullptr)
                {
                    const KBDLLHOOKSTRUCT& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
                    bool injected = (key.flags & LLKHF_INJECTED) != 0;
                    bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
                    if (!injected)
                    {
                        uint32_t keysym = CapturedKeysym(key);
                        // A key swallowed on the way down is swallowed on the way up too, whatever is held by then.
                        bool held = s_instance->m_held.count(key.vkCode) != 0;
                        if (keysym != 0 || (!down && held))
                        {
                            if (down)
                            {
                                s_instance->m_held[key.vkCode] = keysym;
                            }
                            else
                            {
                                if (held)
                                {
                                    keysym = s_instance->m_held[key.vkCode];
                                }
                                s_instance->m_held.erase(key.vkCode);
                            }
                            s_instance->m_send(keysym, down);
                            return 1;
                        }
                    }
                }
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }

            void ReleaseHeld()
            {
                for (const auto& held : m_held)
                {
                    m_send(held.second, false);
                }
                m_held.clear();
            }

            SendKey m_send;
            HHOOK m_hook = nullptr;
            std::map<DWORD, uint32_t> m_held; // virtual key -> keysym sent for its press
            static WindowsSystemKeyCapture* s_instance;
        };

        WindowsSystemKeyCapture* WindowsSystemKeyCapture::s_instance = nullptr;
    } // namespace

    bool SystemKeyCapture::IsSupported()
    {
        return true;
    }

    std::string SystemKeyCapture::GetUnsupportedReason()
    {
        return std::string();
    }

    std::unique_ptr<SystemKeyCapture> SystemKeyCapture::Create(SendKey send)
    {
        return std::make_unique<WindowsSystemKeyCapture>(std::move(send));
    }
} // namespace MarconiUI
