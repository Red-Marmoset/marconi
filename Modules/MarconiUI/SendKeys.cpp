// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The shortcuts in the Send Keys menu, grouped by the other computer's system and then by what they do
//
// Over VNC a Mac's Command key is Super (the Windows key), and Option is Alt. Digits are sent as the digit with
// Shift held, as typing them does.

#include "SendKeys.h"

#include <MarconiCore/Keysyms.h>

namespace MarconiUI
{
    namespace
    {
        namespace Keysym = Marconi::Keysym;

        constexpr uint32_t kCtrl = Keysym::Control_L;
        constexpr uint32_t kAlt = Keysym::Alt_L;
        constexpr uint32_t kShift = Keysym::Shift_L;
        constexpr uint32_t kWin = Keysym::Super_L;
        constexpr uint32_t kCmd = Keysym::Super_L;
        constexpr uint32_t kOption = Keysym::Alt_L;

        std::vector<SendKeysShortcut> MakeShortcuts()
        {
            constexpr RemoteSystem kWindows = RemoteSystem::Windows;
            constexpr RemoteSystem kMac = RemoteSystem::Mac;
            constexpr RemoteSystem kLinux = RemoteSystem::Linux;
            std::vector<SendKeysShortcut> shortcuts = {
                {kWindows, "System", "Ctrl+Alt+Del", {kCtrl, kAlt, Keysym::Delete}},
                {kWindows, "System", "Ctrl+Shift+Esc (Task Manager)", {kCtrl, kShift, Keysym::Escape}},
                {kWindows, "System", "Win+L (lock the screen)", {kWin, 'l'}},
                {kWindows, "System", "Windows Key (Start menu)", {kWin}},
                {kWindows, "System", "Ctrl+Esc (Start menu)", {kCtrl, Keysym::Escape}},
                {kWindows, "Windows", "Alt+Tab (switch windows)", {kAlt, Keysym::Tab}},
                {kWindows, "Windows", "Alt+F4 (close the window)", {kAlt, Keysym::F1 + 3}},
                {kWindows, nullptr, "Print Screen", {Keysym::Print}},

                {kMac, "System", "Cmd+Space (Spotlight)", {kCmd, ' '}},
                {kMac, "System", "Cmd+Option+Esc (Force Quit)", {kCmd, kOption, Keysym::Escape}},
                {kMac, "System", "Cmd+Ctrl+Q (lock the screen)", {kCmd, kCtrl, 'q'}},
                {kMac, "Apps", "Cmd+Tab (switch apps)", {kCmd, Keysym::Tab}},
                {kMac, "Apps", "Cmd+Q (quit the app)", {kCmd, 'q'}},
                {kMac, "Apps", "Cmd+H (hide the app)", {kCmd, 'h'}},
                {kMac, "Apps", "Cmd+M (minimise the window)", {kCmd, 'm'}},
                {kMac, "Screenshots", "Cmd+Shift+3 (the whole screen)", {kCmd, kShift, '3'}},
                {kMac, "Screenshots", "Cmd+Shift+4 (an area)", {kCmd, kShift, '4'}},
                {kMac, "Screenshots", "Cmd+Shift+5 (the screenshot tool)", {kCmd, kShift, '5'}},
                {kMac, "Screenshots", "Cmd+Ctrl+Shift+3 (copy the whole screen)", {kCmd, kCtrl, kShift, '3'}},
                {kMac, "Screenshots", "Cmd+Ctrl+Shift+4 (copy an area)", {kCmd, kCtrl, kShift, '4'}},

                {kLinux, "System", "Win+L (lock the screen)", {kWin, 'l'}},
                {kLinux, "System", "Alt+F2 (run a command)", {kAlt, Keysym::F1 + 1}},
                {kLinux, "System", "Ctrl+Alt+T (open a terminal)", {kCtrl, kAlt, 't'}},
            };
            // Ctrl+Alt+F1 to F7 switch to the computer's text screens (only on a computer with a real screen).
            static const char* const kConsoleLabels[] = {"Ctrl+Alt+F1", "Ctrl+Alt+F2", "Ctrl+Alt+F3", "Ctrl+Alt+F4",
                                                         "Ctrl+Alt+F5", "Ctrl+Alt+F6", "Ctrl+Alt+F7"};
            for (uint32_t i = 0; i < 7; ++i)
            {
                shortcuts.push_back({kLinux, "Text Screens", kConsoleLabels[i], {kCtrl, kAlt, Keysym::F1 + i}});
            }
            return shortcuts;
        }
    } // namespace

    const std::vector<SendKeysShortcut>& GetSendKeysShortcuts()
    {
        static const std::vector<SendKeysShortcut> shortcuts = MakeShortcuts();
        return shortcuts;
    }

    const char* GetRemoteSystemName(RemoteSystem system)
    {
        switch (system)
        {
        case RemoteSystem::Windows: return "Windows";
        case RemoteSystem::Mac: return "Mac";
        case RemoteSystem::Linux: return "Linux";
        }
        return "";
    }
} // namespace MarconiUI
