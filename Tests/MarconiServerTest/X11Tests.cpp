// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux X11 capture and input, against a real X server (run under Xvfb: DISPLAY=:93 with an Xvfb running there).
// A helper here draws a known colour pattern with Xlib; the checks read it back through the server's screen source
// (pixels and changed rectangles) and watch the server's input arrive in an X window of the test's own. Without a
// DISPLAY these tests print SKIP and pass, so the suite still runs on a machine with no X server.

#include "TestSupport.h"

#if defined(__linux__)

#include <MarconiServerCore/SystemInput.h>
#include <MarconiServerCore/SystemScreen.h>

#include <MarconiCore/Keysyms.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        constexpr uint32_t kRed = 0xFF0000;
        constexpr uint32_t kGreen = 0x00FF00;
        constexpr uint32_t kBlue = 0x0000FF;
        constexpr uint32_t kYellow = 0xFFFF00;
        constexpr uint32_t kWhite = 0xFFFFFF;

        // Draws straight onto the root window, which is what the server captures.
        class PatternDrawer
        {
        public:
            explicit PatternDrawer(Display* display) : m_display(display), m_root(DefaultRootWindow(display))
            {
                m_gc = XCreateGC(display, m_root, 0, nullptr);
            }

            ~PatternDrawer() { XFreeGC(m_display, m_gc); }

            void Fill(uint32_t colour, int x, int y, int width, int height)
            {
                XSetForeground(m_display, m_gc, colour);
                XFillRectangle(m_display, m_root, m_gc, x, y, static_cast<unsigned>(width),
                               static_cast<unsigned>(height));
                XSync(m_display, False);
            }

            int Width() const { return DisplayWidth(m_display, DefaultScreen(m_display)); }
            int Height() const { return DisplayHeight(m_display, DefaultScreen(m_display)); }

        private:
            Display* m_display;
            Window m_root;
            GC m_gc;
        };

        bool Covers(const std::vector<Wire::Rect>& rects, int x, int y)
        {
            for (const auto& rect : rects)
            {
                if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height)
                {
                    return true;
                }
            }
            return false;
        }

        // An X window of the test's own that listens for the pointer and keyboard, to see what the input sink does.
        class Observer
        {
        public:
            explicit Observer(Display* display) : m_display(display)
            {
                int screen = DefaultScreen(display);
                XSetWindowAttributes attributes = {};
                attributes.override_redirect = True;
                attributes.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                                        PointerMotionMask;
                m_window = XCreateWindow(display, RootWindow(display, screen), 200, 150, 300, 300, 0, CopyFromParent,
                                         InputOutput, CopyFromParent, CWOverrideRedirect | CWEventMask, &attributes);
                XMapRaised(display, m_window);
                XSetInputFocus(display, m_window, RevertToParent, CurrentTime);
                XSync(display, False);
            }

            ~Observer() { XDestroyWindow(m_display, m_window); XSync(m_display, False); }

            // Waits up to a second and a half for an event of this type; false if none came.
            bool Wait(int type, XEvent& event)
            {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    while (XPending(m_display) > 0)
                    {
                        XNextEvent(m_display, &event);
                        if (event.type == type)
                        {
                            return true;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return false;
            }

            void Drain()
            {
                XSync(m_display, False);
                XEvent event;
                while (XPending(m_display) > 0)
                {
                    XNextEvent(m_display, &event);
                }
            }

            bool PointerAt(int x, int y)
            {
                Window root, child;
                int rootX = 0, rootY = 0, winX, winY;
                unsigned mask;
                XQueryPointer(m_display, DefaultRootWindow(m_display), &root, &child, &rootX, &rootY, &winX, &winY,
                              &mask);
                return rootX == x && rootY == y;
            }

        private:
            Display* m_display;
            Window m_window;
        };

        bool PressedButton(Observer& observer, int type, unsigned button)
        {
            XEvent event;
            while (observer.Wait(type, event))
            {
                if (event.xbutton.button == button)
                {
                    return true;
                }
            }
            return false;
        }

        // The next key event of this type: its keysym, as the observer's own keyboard map says.
        bool NextKey(Observer& observer, int type, XEvent& event)
        {
            return observer.Wait(type, event);
        }
    } // namespace

    void RunX11Tests()
    {
        Display* display = XOpenDisplay(nullptr);
        if (display == nullptr)
        {
            printf("SKIP X11 capture and input tests: no X display (start Xvfb and set DISPLAY)\n");
            return;
        }
        Visual* visual = DefaultVisual(display, DefaultScreen(display));
        if (visual->red_mask != 0xFF0000 || visual->green_mask != 0x00FF00 || visual->blue_mask != 0x0000FF)
        {
            printf("SKIP X11 capture and input tests: the display isn't 24-bit RGB (use Xvfb with -screen 0 WxHx24)\n");
            XCloseDisplay(display);
            return;
        }

        RunTest("X11 capture: the grabbed pixels match the pattern drawn with Xlib", [&]() {
            PatternDrawer drawer(display);
            const int width = drawer.Width();
            const int height = drawer.Height();
            CHECK(width >= 400 && height >= 300);
            drawer.Fill(kWhite, 0, 0, width, height);
            drawer.Fill(kRed, 0, 0, 100, 100);
            drawer.Fill(kGreen, 100, 0, 100, 100);
            drawer.Fill(kBlue, 0, 100, 100, 100);

            std::string error;
            auto screen = Server::CreateSystemScreen(error);
            CHECK(screen != nullptr);
            if (!screen)
            {
                printf("  (%s)\n", error.c_str());
                return;
            }
            auto displays = screen->GetDisplays();
            CHECK(displays.size() == 1);
            CHECK(displays[0].width == width && displays[0].height == height);

            Server::Capture capture;
            CHECK(screen->Grab(capture));
            CHECK(capture.width == width && capture.height == height);
            CHECK(capture.pixels.size() == static_cast<size_t>(width) * height);
            CHECK(capture.dirty.size() == 1);
            if (capture.dirty.size() == 1)
            {
                CHECK(capture.dirty[0].x == 0 && capture.dirty[0].y == 0);
                CHECK(capture.dirty[0].width == width && capture.dirty[0].height == height);
            }
            CHECK(capture.Row(10)[10] == kRed);
            CHECK(capture.Row(99)[99] == kRed);
            CHECK(capture.Row(10)[150] == kGreen);
            CHECK(capture.Row(150)[10] == kBlue);
            CHECK(capture.Row(150)[150] == kWhite);
            CHECK(capture.Row(height - 1)[width - 1] == kWhite);
            CHECK((capture.Row(10)[10] & 0xFF000000u) == 0); // 0x00RRGGBB: the top byte is unused

            // Nothing drawn since: nothing changed.
            CHECK(screen->Grab(capture));
            CHECK(capture.dirty.empty());
        });

        RunTest("X11 capture: dirty rectangles are the changed 64x64 tiles, joined along a row", [&]() {
            PatternDrawer drawer(display);
            const int width = drawer.Width();
            const int height = drawer.Height();
            drawer.Fill(kWhite, 0, 0, width, height);

            std::string error;
            auto screen = Server::CreateSystemScreen(error);
            CHECK(screen != nullptr);
            if (!screen)
            {
                return;
            }
            Server::Capture capture;
            CHECK(screen->Grab(capture)); // the first grab is all dirty

            // One small change inside the tile at x 128..191, y 64..127.
            drawer.Fill(kYellow, 130, 70, 10, 10);
            CHECK(screen->Grab(capture));
            CHECK(capture.dirty.size() == 1);
            if (capture.dirty.size() == 1)
            {
                CHECK(capture.dirty[0].x == 128 && capture.dirty[0].y == 64);
                CHECK(capture.dirty[0].width == 64 && capture.dirty[0].height == 64);
            }
            CHECK(capture.Row(75)[135] == kYellow);

            // A change across a tile edge (x 190..194): two neighbouring tiles become one rectangle. The previous
            // yellow square is still there, so its tile is dirty only if it changes; paint it over to be sure.
            drawer.Fill(kWhite, 130, 70, 10, 10);
            CHECK(screen->Grab(capture));
            drawer.Fill(kRed, 190, 10, 5, 5);
            CHECK(screen->Grab(capture));
            CHECK(capture.dirty.size() == 1);
            if (capture.dirty.size() == 1)
            {
                CHECK(capture.dirty[0].x == 128 && capture.dirty[0].y == 0);
                CHECK(capture.dirty[0].width == 128 && capture.dirty[0].height == 64);
            }

            // Two changes on different tile rows are two rectangles, and they cover the changed pixels.
            drawer.Fill(kBlue, 5, 5, 3, 3);
            drawer.Fill(kBlue, 5, 200, 3, 3);
            CHECK(screen->Grab(capture));
            CHECK(capture.dirty.size() == 2);
            CHECK(Covers(capture.dirty, 6, 6));
            CHECK(Covers(capture.dirty, 6, 201));
            CHECK(!Covers(capture.dirty, 300, 100));

            // A change in the last, partly filled tile (bottom-right corner) stays inside the screen.
            drawer.Fill(kGreen, width - 2, height - 2, 2, 2);
            CHECK(screen->Grab(capture));
            CHECK(capture.dirty.size() == 1);
            if (capture.dirty.size() == 1)
            {
                const auto& rect = capture.dirty[0];
                CHECK(rect.x + rect.width == width);
                CHECK(rect.y + rect.height == height);
                CHECK(rect.width >= 1 && rect.width <= 64 && rect.height >= 1 && rect.height <= 64);
            }
            CHECK(capture.Row(height - 1)[width - 1] == kGreen);
        });

        RunTest("X11 input: pointer, buttons and wheel reach an X client", [&]() {
            std::string error;
            auto input = Server::CreateSystemInput(error);
            CHECK(input != nullptr);
            if (!input)
            {
                printf("  (%s)\n", error.c_str());
                return;
            }
            Observer observer(display);
            observer.Drain();

            input->MovePointer(300, 200, 0, 0, 0);
            XSync(display, False);
            CHECK(observer.PointerAt(300, 200));
            observer.Drain();

            input->MovePointer(300, 200, 0x01, 0, 0);
            CHECK(PressedButton(observer, ButtonPress, Button1));
            input->MovePointer(300, 200, 0x00, 0, 0);
            CHECK(PressedButton(observer, ButtonRelease, Button1));

            input->MovePointer(310, 210, 0x04, 0, 0);
            CHECK(PressedButton(observer, ButtonPress, Button3));
            input->MovePointer(310, 210, 0x00, 0, 0);
            CHECK(PressedButton(observer, ButtonRelease, Button3));
            CHECK(observer.PointerAt(310, 210));

            input->MovePointer(310, 210, 0x02, 0, 0);
            CHECK(PressedButton(observer, ButtonPress, Button2));
            input->MovePointer(310, 210, 0x00, 0, 0);
            CHECK(PressedButton(observer, ButtonRelease, Button2));

            input->MovePointer(310, 210, 0, 120, 0);
            CHECK(PressedButton(observer, ButtonPress, Button4)); // up
            input->MovePointer(310, 210, 0, -120, 0);
            CHECK(PressedButton(observer, ButtonPress, Button5)); // down
            input->MovePointer(310, 210, 0, 0, 120);
            CHECK(PressedButton(observer, ButtonPress, 7)); // right
            input->MovePointer(310, 210, 0, 0, -120);
            CHECK(PressedButton(observer, ButtonPress, 6)); // left

            // Two notches make two presses.
            observer.Drain();
            input->MovePointer(310, 210, 0, 240, 0);
            int presses = 0;
            XEvent event;
            while (observer.Wait(ButtonPress, event))
            {
                if (event.xbutton.button == Button4)
                {
                    ++presses;
                }
                if (presses == 2)
                {
                    break;
                }
            }
            CHECK(presses == 2);
        });

        RunTest("X11 input: keys and typed text reach an X client", [&]() {
            std::string error;
            auto input = Server::CreateSystemInput(error);
            CHECK(input != nullptr);
            if (!input)
            {
                return;
            }
            Observer observer(display);
            input->MovePointer(300, 200, 0, 0, 0); // keys go to the focused window; the pointer is over it too
            observer.Drain();

            input->SendKey(Keysym::FromUnicode('a'), 0, true);
            XEvent event;
            CHECK(NextKey(observer, KeyPress, event));
            CHECK(XLookupKeysym(&event.xkey, 0) == XK_a);
            input->SendKey(Keysym::FromUnicode('a'), 0, false);
            CHECK(NextKey(observer, KeyRelease, event));
            CHECK(XLookupKeysym(&event.xkey, 0) == XK_a);

            input->SendKey(Keysym::Shift_L, 0, true);
            input->SendKey(Keysym::FromUnicode('b'), 0, true);
            bool sawShifted = false;
            while (NextKey(observer, KeyPress, event))
            {
                if (XLookupKeysym(&event.xkey, 0) == XK_b)
                {
                    sawShifted = (event.xkey.state & ShiftMask) != 0;
                    break;
                }
            }
            CHECK(sawShifted);
            input->SendKey(Keysym::FromUnicode('b'), 0, false);
            input->SendKey(Keysym::Shift_L, 0, false);
            observer.Drain();

            input->SendKey(Keysym::Return, 0, true);
            CHECK(NextKey(observer, KeyPress, event));
            CHECK(XLookupKeysym(&event.xkey, 0) == XK_Return);
            input->SendKey(Keysym::Return, 0, false);
            observer.Drain();

            // A character with no key on the keyboard (the euro sign) is typed with a borrowed key, which is
            // given back afterwards.
            int minimum = 0;
            int maximum = 0;
            XDisplayKeycodes(display, &minimum, &maximum);
            input->TypeText("\xE2\x82\xAC");
            CHECK(NextKey(observer, KeyPress, event));
            CHECK(event.xkey.keycode >= static_cast<unsigned>(minimum));
            CHECK(NextKey(observer, KeyRelease, event));
            observer.Drain();
            int perKey = 0;
            KeySym* symbols = XGetKeyboardMapping(display, static_cast<KeyCode>(maximum), 1, &perKey);
            CHECK(symbols != nullptr && symbols[0] == NoSymbol);
            if (symbols != nullptr)
            {
                XFree(symbols);
            }

            // The same character sent as a key press (what a viewer's keyboard sends) is typed too.
            input->SendKey(Keysym::FromUnicode(0x20AC), 0, true);
            CHECK(NextKey(observer, KeyPress, event));
            CHECK(NextKey(observer, KeyRelease, event));
            input->SendKey(Keysym::FromUnicode(0x20AC), 0, false); // nothing more to do
            observer.Drain();
        });

        XCloseDisplay(display);
    }
} // namespace MarconiServerTest

#else

namespace MarconiServerTest
{
    void RunX11Tests() {}
} // namespace MarconiServerTest

#endif
