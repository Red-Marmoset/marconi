// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: opening the X display with plain-words errors, and catching X errors that would otherwise end the program

#pragma once

#include <X11/Xlib.h>

#include <atomic>
#include <cstdlib>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // True when the person is logged in to a Wayland session. Even if an X server (XWayland) is reachable then,
        // it only holds the old-style windows, so the screen picture would be mostly black and keys and clicks
        // wouldn't reach the other programs.
        inline bool IsWaylandSession()
        {
            const char* type = std::getenv("XDG_SESSION_TYPE");
            return type != nullptr && std::string(type) == "wayland";
        }

        // Explains in plain words why there is no usable X display, from the environment.
        inline std::string DescribeNoX11Display()
        {
            const char* display = std::getenv("DISPLAY");
            const char* wayland = std::getenv("WAYLAND_DISPLAY");
            if (IsWaylandSession() || ((display == nullptr || *display == '\0') && wayland != nullptr && *wayland != '\0'))
            {
                return "This computer is running a Wayland desktop, and Marconi Server can only share an X11 "
                       "desktop for now. Log out and choose the X11 option on the login screen (for example "
                       "\"Ubuntu on Xorg\") to share this computer's screen.";
            }
            if (display == nullptr || *display == '\0')
            {
                return "There is no desktop to share: this program was started without a graphical session (for "
                       "example from a plain terminal or over SSH). Start it from the desktop, or set DISPLAY to the "
                       "screen to share, for example DISPLAY=:0.";
            }
            return std::string("Could not connect to the desktop \"") + display +
                   "\". Check that it is running and that this user is allowed to use it.";
        }

        // Opens the display. On failure `error` says why in plain words.
        inline bool X11OpenDisplay(Display*& display, std::string& error)
        {
            if (IsWaylandSession())
            {
                error = DescribeNoX11Display();
                return false;
            }
            display = XOpenDisplay(nullptr);
            if (display == nullptr)
            {
                error = DescribeNoX11Display();
                return false;
            }
            return true;
        }

        // While one of these exists, X errors set a flag instead of ending the program (Xlib's default). The error
        // handler is shared by the whole program, so keep the scope short.
        class X11ScopedErrorCatcher
        {
        public:
            X11ScopedErrorCatcher()
            {
                Flag().store(false);
                m_previous = XSetErrorHandler(&X11ScopedErrorCatcher::Handle);
            }

            ~X11ScopedErrorCatcher() { XSetErrorHandler(m_previous); }

            X11ScopedErrorCatcher(const X11ScopedErrorCatcher&) = delete;
            X11ScopedErrorCatcher& operator=(const X11ScopedErrorCatcher&) = delete;

            // True if an X error arrived since this was made. Call XSync first so errors are delivered.
            bool Failed() const { return Flag().load(); }

        private:
            static std::atomic<bool>& Flag()
            {
                static std::atomic<bool> flag(false);
                return flag;
            }

            static int Handle(Display*, XErrorEvent*)
            {
                Flag().store(true);
                return 0;
            }

            int (*m_previous)(Display*, XErrorEvent*) = nullptr;
        };
    } // namespace Server
} // namespace Marconi
