// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Translates wx keyboard events into X11 keysyms and tracks which keys are held

#include "KeyMapper.h"

#include <MarconiCore/Keysyms.h>

#include <wx/event.h>

namespace MarconiUI
{
    namespace Keysym = Marconi::Keysym;

    long KeyMapper::KeyId(const wxKeyEvent& event)
    {
#if defined(__WXGTK__)
        // On GTK the raw key code is the keyval, which changes with Shift; the hardware keycode doesn't.
        return static_cast<long>(event.GetRawKeyFlags());
#else
        // Windows virtual key / macOS hardware key code: stable between down and up.
        return static_cast<long>(event.GetRawKeyCode());
#endif
    }

    uint32_t KeyMapper::SpecialKeysym(const wxKeyEvent& event)
    {
        int code = event.GetKeyCode();
        if (code >= WXK_F1 && code <= WXK_F24)
        {
            return Keysym::F1 + static_cast<uint32_t>(code - WXK_F1);
        }
        if (code >= WXK_NUMPAD0 && code <= WXK_NUMPAD9)
        {
            return Keysym::KP_0 + static_cast<uint32_t>(code - WXK_NUMPAD0);
        }

        switch (code)
        {
        case WXK_BACK:
            return Keysym::BackSpace;
        case WXK_TAB:
            return Keysym::Tab;
        case WXK_RETURN:
            return Keysym::Return;
        case WXK_ESCAPE:
            return Keysym::Escape;
        case WXK_DELETE:
            return Keysym::Delete;
        case WXK_INSERT:
            return Keysym::Insert;
        case WXK_HOME:
            return Keysym::Home;
        case WXK_END:
            return Keysym::End;
        case WXK_PAGEUP:
            return Keysym::PageUp;
        case WXK_PAGEDOWN:
            return Keysym::PageDown;
        case WXK_LEFT:
            return Keysym::Left;
        case WXK_RIGHT:
            return Keysym::Right;
        case WXK_UP:
            return Keysym::Up;
        case WXK_DOWN:
            return Keysym::Down;
        case WXK_PAUSE:
            return Keysym::Pause;
        case WXK_SCROLL:
            return Keysym::ScrollLock;
        case WXK_SNAPSHOT:
        case WXK_PRINT:
            return Keysym::Print;
        case WXK_HELP:
            return Keysym::Help;
        case WXK_MENU:
        case WXK_WINDOWS_MENU:
            return Keysym::Menu;
        case WXK_CAPITAL:
            return Keysym::CapsLock;
        case WXK_NUMLOCK:
            return Keysym::NumLock;

        case WXK_SHIFT:
            return Keysym::Shift_L;
        case WXK_ALT:
            return Keysym::Alt_L;
#if defined(__WXOSX__)
        // wx reports Command as WXK_CONTROL and the physical Control key as WXK_RAW_CONTROL. Command maps to
        // Super, which macOS servers treat as Command and Windows/Linux servers as the Windows/Super key.
        case WXK_CONTROL:
            return Keysym::Super_L;
        case WXK_RAW_CONTROL:
            return Keysym::Control_L;
#else
        case WXK_CONTROL:
            return Keysym::Control_L;
#endif
        case WXK_WINDOWS_LEFT:
            return Keysym::Super_L;
        case WXK_WINDOWS_RIGHT:
            return Keysym::Super_R;

        case WXK_NUMPAD_ENTER:
            return Keysym::KP_Enter;
        case WXK_NUMPAD_HOME:
            return Keysym::KP_Home;
        case WXK_NUMPAD_LEFT:
            return Keysym::KP_Left;
        case WXK_NUMPAD_UP:
            return Keysym::KP_Up;
        case WXK_NUMPAD_RIGHT:
            return Keysym::KP_Right;
        case WXK_NUMPAD_DOWN:
            return Keysym::KP_Down;
        case WXK_NUMPAD_PAGEUP:
            return Keysym::KP_PageUp;
        case WXK_NUMPAD_PAGEDOWN:
            return Keysym::KP_PageDown;
        case WXK_NUMPAD_END:
            return Keysym::KP_End;
        case WXK_NUMPAD_BEGIN:
            return Keysym::KP_Begin;
        case WXK_NUMPAD_INSERT:
            return Keysym::KP_Insert;
        case WXK_NUMPAD_DELETE:
            return Keysym::KP_Delete;
        case WXK_NUMPAD_MULTIPLY:
            return Keysym::KP_Multiply;
        case WXK_NUMPAD_ADD:
            return Keysym::KP_Add;
        case WXK_NUMPAD_SEPARATOR:
            return Keysym::KP_Separator;
        case WXK_NUMPAD_SUBTRACT:
            return Keysym::KP_Subtract;
        case WXK_NUMPAD_DECIMAL:
            return Keysym::KP_Decimal;
        case WXK_NUMPAD_DIVIDE:
            return Keysym::KP_Divide;
        case WXK_NUMPAD_EQUAL:
            return Keysym::KP_Equal;
        default:
            return 0;
        }
    }

    uint32_t KeyMapper::CharKeysym(const wxKeyEvent& event)
    {
        wxChar character = event.GetUnicodeKey();
        if (character == WXK_NONE)
        {
            return 0;
        }
        uint32_t codepoint = static_cast<uint32_t>(character);
        if (codepoint < 0x20)
        {
            // Ctrl+letter arrives as a control character (Ctrl+A = 1); send the letter, the server already knows
            // Ctrl is down.
            if (codepoint >= 1 && codepoint <= 26)
            {
                return (event.ShiftDown() ? 'A' : 'a') + codepoint - 1;
            }
            static const char kControlPunctuation[] = {'@', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                       0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '[', '\\', ']', '^', '_'};
            return static_cast<uint32_t>(kControlPunctuation[codepoint]);
        }
        return Keysym::FromUnicode(codepoint);
    }

    void KeyMapper::Press(long keyId, uint32_t keysym)
    {
        auto found = m_pressed.find(keyId);
        if (found != m_pressed.end() && found->second != keysym)
        {
            // Same physical key now producing a different symbol (e.g. modifiers changed mid-repeat).
            m_send(found->second, false);
        }
        m_pressed[keyId] = keysym;
        m_send(keysym, true);
    }

    bool KeyMapper::OnKeyDown(const wxKeyEvent& event)
    {
        uint32_t keysym = SpecialKeysym(event);
        if (keysym == 0)
        {
            m_charKeyId = KeyId(event);
            return false; // wait for wxEVT_CHAR
        }
        Press(KeyId(event), keysym);
        return true;
    }

    void KeyMapper::OnChar(const wxKeyEvent& event)
    {
        uint32_t keysym = CharKeysym(event);
        long keyId = m_charKeyId != -1 ? m_charKeyId : KeyId(event);
        m_charKeyId = -1;
        if (keysym == 0)
        {
            return;
        }

#if defined(__WXMSW__)
        // Windows reports AltGr as Ctrl+Alt. A printable character typed with both held is an AltGr character:
        // lift the fake modifiers around it, otherwise the server sees Ctrl+Alt+<char>.
        bool altGr = event.ControlDown() && event.AltDown() && keysym >= 0x20 &&
                     (event.GetUnicodeKey() >= 0x20);
        if (altGr)
        {
            for (const auto& pressed : m_pressed)
            {
                if (pressed.second == Keysym::Control_L || pressed.second == Keysym::Alt_L)
                {
                    m_send(pressed.second, false);
                }
            }
            Press(keyId, keysym);
            for (const auto& pressed : m_pressed)
            {
                if (pressed.second == Keysym::Control_L || pressed.second == Keysym::Alt_L)
                {
                    m_send(pressed.second, true);
                }
            }
            return;
        }
#endif
        Press(keyId, keysym);
    }

    void KeyMapper::OnKeyUp(const wxKeyEvent& event)
    {
        auto found = m_pressed.find(KeyId(event));
        if (found == m_pressed.end())
        {
            return;
        }
        uint32_t keysym = found->second;
        m_pressed.erase(found);
        m_send(keysym, false);
    }

    void KeyMapper::ReleaseAll()
    {
        for (const auto& pressed : m_pressed)
        {
            m_send(pressed.second, false);
        }
        m_pressed.clear();
    }
} // namespace MarconiUI
