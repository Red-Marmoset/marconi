// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Makes the program aware of high-resolution screens on Windows, so it sees and captures the screen at its real size

#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Marconi
{
    namespace Server
    {
#ifdef _WIN32
        // On a screen scaled to 150% or more, a program that doesn't say it understands that is told a smaller screen
        // size, and the server then captures only the top-left part of the real picture (shown hugely magnified in the
        // viewer) and puts the pointer in the wrong place. The manifest that goes with the program asks for this too;
        // this call is a second layer for a program started without it. It only works before the program creates a
        // window, so call it first thing (the tray program does it from a static initialiser). Not yet built on
        // Windows. Returns true when the program is aware, whether this call or the manifest made it so.
        inline bool EnableDpiAwareness()
        {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32 == nullptr)
            {
                return false;
            }
            // Windows 10 1703 and later: per-monitor awareness, version 2 (the "context" handles are small negative
            // numbers: -4 is per-monitor v2, -3 is per-monitor).
            using SetContextFunction = BOOL(WINAPI*)(HANDLE);
            auto setContext = reinterpret_cast<SetContextFunction>(
                reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
            if (setContext != nullptr)
            {
                if (setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))))
                {
                    return true;
                }
                if (GetLastError() == ERROR_ACCESS_DENIED)
                {
                    return true; // already set, by the manifest
                }
                if (setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3))))
                {
                    return true;
                }
            }
            // Older Windows: system-wide awareness.
            using SetAwareFunction = BOOL(WINAPI*)();
            auto setAware =
                reinterpret_cast<SetAwareFunction>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDPIAware")));
            return setAware != nullptr && setAware() != 0;
        }
#else
        inline bool EnableDpiAwareness()
        {
            return true; // only Windows has this problem
        }
#endif
    } // namespace Server
} // namespace Marconi
