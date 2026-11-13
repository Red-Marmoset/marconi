// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Following the input desktop (UAC prompts, lock screen) and sending Ctrl+Alt+Del on Windows.
//
// NOTE: not yet built or run on Windows from this environment (the cloud build is Linux). Everything here needs
// trying on a real machine as SYSTEM in the console session: see the Phase 3 checklist in SERVER_PLAN.md.

#include "DesktopSwitch.h"

#include "ServiceNames.h"

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
            std::string NarrowName(HDESK desktop)
            {
                wchar_t name[256] = {};
                DWORD needed = 0;
                if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed))
                {
                    return std::string();
                }
                char utf8[512] = {};
                int length = WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
                return length > 1 ? std::string(utf8, static_cast<size_t>(length - 1)) : std::string();
            }

            // Each thread keeps the desktop handle it last switched to, because a thread's desktop handle must
            // stay open while the thread uses it.
            struct ThreadDesktop
            {
                HDESK handle = nullptr;
                std::string name;
                ULONGLONG lastCheck = 0;
                ~ThreadDesktop()
                {
                    // Closing is safe only once the thread no longer uses it; this runs as the thread ends.
                    if (handle != nullptr)
                    {
                        CloseDesktop(handle);
                    }
                }
            };
            thread_local ThreadDesktop t_desktop;
        } // namespace

        bool FollowInputDesktop()
        {
            ULONGLONG now = GetTickCount64();
            if (t_desktop.lastCheck != 0 && now - t_desktop.lastCheck < 50)
            {
                return false;
            }
            t_desktop.lastCheck = now;

            // OpenInputDesktop fails while the secure desktop is being switched in; the next call tries again.
            HDESK input = OpenInputDesktop(0, FALSE, GENERIC_ALL);
            if (input == nullptr)
            {
                return false;
            }
            std::string inputName = NarrowName(input);
            if (t_desktop.handle != nullptr && inputName == t_desktop.name)
            {
                CloseDesktop(input); // already on it
                return false;
            }
            if (!SetThreadDesktop(input))
            {
                // Fails if this thread has windows or hooks on its current desktop; the capture and input threads
                // have none. Stay where we are.
                CloseDesktop(input);
                return false;
            }
            HDESK previous = t_desktop.handle;
            t_desktop.handle = input;
            t_desktop.name = inputName;
            if (previous != nullptr)
            {
                CloseDesktop(previous);
            }
            // True also for the first attach: a device context taken before it belongs to the old desktop.
            return true;
        }

        std::string GetInputDesktopName()
        {
            HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            if (input == nullptr)
            {
                return std::string();
            }
            std::string name = NarrowName(input);
            CloseDesktop(input);
            return name;
        }

        bool SendSecureAttention()
        {
            // Preferred: the service does it, from a service process, which is what SendSAS accepts by default.
            HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, kSasEventName);
            if (request != nullptr)
            {
                BOOL ok = SetEvent(request);
                CloseHandle(request);
                if (ok)
                {
                    return true;
                }
            }
            // A helper run by hand: SendSAS from sas.dll, which needs the software SAS generation policy to allow
            // applications (not only services) to work from here.
            HMODULE sas = LoadLibraryW(L"sas.dll");
            if (sas == nullptr)
            {
                return false;
            }
            using SendSasFunction = VOID(WINAPI*)(BOOL);
            SendSasFunction sendSas = reinterpret_cast<SendSasFunction>(GetProcAddress(sas, "SendSAS"));
            bool sent = false;
            if (sendSas != nullptr)
            {
                sendSas(FALSE);
                sent = true;
            }
            FreeLibrary(sas);
            return sent;
        }
    } // namespace Server
} // namespace Marconi
