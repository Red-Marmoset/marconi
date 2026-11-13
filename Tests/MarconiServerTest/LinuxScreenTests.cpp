// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Linux screen source against a real X server (run under Xvfb with +extension RANDR): the display layout, choosing
// one monitor, changing an output's mode and putting it back, and the pointer. Skipped when there is no X display.

#include "TestSupport.h"

#if defined(__linux__)

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/SystemScreen.h>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#undef Status

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return predicate();
        }

        void Fill(Display* display, Window root, int x, int y, int width, int height, unsigned long colour)
        {
            GC gc = XCreateGC(display, root, 0, nullptr);
            XSetForeground(display, gc, colour);
            XFillRectangle(display, root, gc, x, y, static_cast<unsigned>(width), static_cast<unsigned>(height));
            XFreeGC(display, gc);
            XSync(display, False);
        }

        const Wire::Display* Find(const std::vector<Wire::Display>& displays, uint32_t id)
        {
            for (const Wire::Display& d : displays)
            {
                if (d.id == id)
                {
                    return &d;
                }
            }
            return nullptr;
        }
    } // namespace

    void RunLinuxScreenTests()
    {
        Display* x = XOpenDisplay(nullptr);
        if (x == nullptr)
        {
            printf("\n(Linux screen tests skipped: no X display; start Xvfb with +extension RANDR and set DISPLAY)\n");
            return;
        }
        Window root = DefaultRootWindow(x);

        RunTest("Linux: the layout lists the monitors, including virtual ones, and the capture covers the desktop", []() {
            std::string error;
            std::unique_ptr<Server::ScreenSource> screen = Server::CreateSystemScreen(error);
            CHECK(screen != nullptr);
            if (!screen)
            {
                return;
            }
            std::vector<Wire::Display> before = screen->GetDisplays();
            CHECK(!before.empty());
            for (const Wire::Display& d : before)
            {
                printf("  display %u: %dx%d at %d,%d\n", d.id, d.width, d.height, d.x, d.y);
            }
            Server::Capture capture;
            CHECK(screen->Grab(capture));
            CHECK(capture.width > 0 && capture.height > 0);
            CHECK(capture.dirty.size() == 1); // the first grab is the whole desktop
            CHECK(screen->CanSetDisplayMode());
        });

        RunTest("Linux: choosing one of two monitors streams only that monitor", [x, root]() {
            int width = DisplayWidth(x, DefaultScreen(x));
            int height = DisplayHeight(x, DefaultScreen(x));
            int half = width / 2;
            // Two virtual monitors side by side, as `xrandr --setmonitor` makes them.
            Atom leftName = XInternAtom(x, "MarconiTestLeft", False);
            Atom rightName = XInternAtom(x, "MarconiTestRight", False);
            XRRMonitorInfo* left = XRRAllocateMonitor(x, 0);
            left->name = leftName;
            left->x = 0;
            left->y = 0;
            left->width = half;
            left->height = height;
            left->mwidth = 100;
            left->mheight = 100;
            left->primary = False;
            left->automatic = False;
            XRRSetMonitor(x, root, left);
            XRRFreeMonitors(left);
            XRRMonitorInfo* right = XRRAllocateMonitor(x, 0);
            right->name = rightName;
            right->x = half;
            right->y = 0;
            right->width = width - half;
            right->height = height;
            right->mwidth = 100;
            right->mheight = 100;
            right->primary = False;
            right->automatic = False;
            XRRSetMonitor(x, root, right);
            XRRFreeMonitors(right);
            XSync(x, False);

            std::string error;
            std::unique_ptr<Server::ScreenSource> probe = Server::CreateSystemScreen(error);
            CHECK(probe != nullptr);
            std::vector<Wire::Display> displays = probe->GetDisplays();
            const Wire::Display* leftDisplay = Find(displays, static_cast<uint32_t>(leftName));
            const Wire::Display* rightDisplay = Find(displays, static_cast<uint32_t>(rightName));
            CHECK(leftDisplay != nullptr && rightDisplay != nullptr);
            if (leftDisplay == nullptr || rightDisplay == nullptr)
            {
                return;
            }
            CHECK(leftDisplay->x == 0 && leftDisplay->width == half);
            CHECK(rightDisplay->x == half && rightDisplay->width == width - half);
            probe.reset();

            const unsigned long kBlack = 0x000000;
            const unsigned long kRed = 0xFF0000;
            const unsigned long kBlue = 0x0000FF;
            const unsigned long kGreen = 0x00FF00;
            Fill(x, root, 0, 0, width, height, kBlack);

            // A real server on the loopback, the real X screen, the real client.
            std::unique_ptr<Server::ScreenSource> screen = Server::CreateSystemScreen(error);
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            settings.port = 0;
            Server::Listener listener(*screen, input, settings);
            listener.SetFrameIntervalMs(5);
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(5000); });
            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            client.Connect(options);
            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(client.GetScreens().size() >= 2);
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));

            auto pixel = [&](int px, int py) {
                Rfb::Framebuffer& fb = client.GetFramebuffer();
                std::lock_guard<std::mutex> lock(fb.GetMutex());
                return fb.GetPixel(px, py);
            };
            CHECK(WaitUntil([&]() { return pixel(10, 10) == 0 && pixel(width - 10, 10) == 0; }));

            client.SelectDisplay(static_cast<uint32_t>(rightName));
            // The wait for the selection to reach the server and its full picture to come back, then paint both
            // halves: only the chosen monitor's paint may arrive.
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            Fill(x, root, 0, 0, half, height, kRed);
            Fill(x, root, half, 0, width - half, height, kBlue);
            CHECK(WaitUntil([&]() { return pixel(width - 10, 10) == kBlue; }));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CHECK(pixel(10, 10) == 0); // the left monitor's red never came

            client.SelectDisplay(0);
            CHECK(WaitUntil([&]() { return pixel(10, 10) == kRed; })); // the whole desktop again

            // Choosing the left monitor and painting only the right one: nothing new arrives.
            client.SelectDisplay(static_cast<uint32_t>(leftName));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            Fill(x, root, half, 0, width - half, height, kGreen);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            CHECK(pixel(width - 10, 10) == kBlue);

            client.Disconnect();
            listener.Stop();
            serverThread.join();
            XRRDeleteMonitor(x, root, leftName);
            XRRDeleteMonitor(x, root, rightName);
            XSync(x, False);
        });

        RunTest("Linux: an output's size changes to a listed mode only, and comes back", [x, root]() {
            std::string error;
            std::unique_ptr<Server::ScreenSource> screen = Server::CreateSystemScreen(error);
            CHECK(screen != nullptr);
            if (!screen)
            {
                return;
            }
            const int originalWidth = DisplayWidth(x, DefaultScreen(x));
            const int originalHeight = DisplayHeight(x, DefaultScreen(x));

            // The monitor with a real output behind it, and the sizes that output lists.
            int count = 0;
            XRRMonitorInfo* monitors = XRRGetMonitors(x, root, True, &count);
            uint32_t id = 0;
            RROutput output = 0;
            for (int i = 0; i < count; ++i)
            {
                if (monitors[i].noutput > 0 && output == 0)
                {
                    id = static_cast<uint32_t>(monitors[i].name);
                    output = monitors[i].outputs[0];
                }
            }
            XRRFreeMonitors(monitors);
            CHECK(output != 0);

            // Xvfb lists just its one size, so give the output two more, as `xrandr --newmode` and `--addmode` do.
            auto addMode = [&](const char* name, int w, int h, unsigned long clock, int hs, int he, int ht, int vs,
                               int ve, int vt) {
                XRRModeInfo modeInfo = {};
                modeInfo.width = static_cast<unsigned>(w);
                modeInfo.height = static_cast<unsigned>(h);
                modeInfo.dotClock = clock;
                modeInfo.hSyncStart = static_cast<unsigned>(hs);
                modeInfo.hSyncEnd = static_cast<unsigned>(he);
                modeInfo.hTotal = static_cast<unsigned>(ht);
                modeInfo.vSyncStart = static_cast<unsigned>(vs);
                modeInfo.vSyncEnd = static_cast<unsigned>(ve);
                modeInfo.vTotal = static_cast<unsigned>(vt);
                modeInfo.name = const_cast<char*>(name);
                modeInfo.nameLength = static_cast<unsigned>(strlen(name));
                RRMode mode = XRRCreateMode(x, root, &modeInfo);
                if (mode != 0)
                {
                    XRRAddOutputMode(x, output, mode);
                }
                XSync(x, False);
                return mode;
            };
            RRMode mode800 = addMode("MarconiTest800", 800, 600, 40000000, 840, 968, 1056, 601, 605, 628);
            RRMode mode640 = addMode("MarconiTest640", 640, 480, 25175000, 656, 752, 800, 490, 492, 525);
            CHECK(mode800 != 0 && mode640 != 0);

            XRRScreenResources* resources = XRRGetScreenResourcesCurrent(x, root);
            XRROutputInfo* info = XRRGetOutputInfo(x, resources, output);
            std::vector<std::pair<int, int>> listed;
            for (int i = 0; i < info->nmode; ++i)
            {
                for (int m = 0; m < resources->nmode; ++m)
                {
                    if (resources->modes[m].id == info->modes[i])
                    {
                        listed.push_back({static_cast<int>(resources->modes[m].width),
                                          static_cast<int>(resources->modes[m].height)});
                    }
                }
            }
            XRRFreeOutputInfo(info);
            XRRFreeScreenResources(resources);
            printf("  the output lists %zu modes:", listed.size());
            for (const auto& mode : listed)
            {
                printf(" %dx%d", mode.first, mode.second);
            }
            printf("\n");
            CHECK(listed.size() >= 2);
            if (listed.size() < 2 || id == 0)
            {
                return;
            }

            // Ask for a listed size other than the current one.
            std::pair<int, int> target = listed[0];
            for (const auto& mode : listed)
            {
                if (mode.first != originalWidth || mode.second != originalHeight)
                {
                    target = mode;
                }
            }
            CHECK(screen->SetDisplayMode(id, target.first, target.second) == Wire::DisplayModeResult::Status::Ok);
            XWindowAttributes attributes;
            XGetWindowAttributes(x, root, &attributes);
            printf("  asked for %dx%d, the desktop is now %dx%d (was %dx%d)\n", target.first, target.second,
                   attributes.width, attributes.height, originalWidth, originalHeight);
            CHECK(attributes.width == target.first && attributes.height == target.second);
            std::vector<Wire::Display> displays = screen->GetDisplays();
            const Wire::Display* changed = Find(displays, id);
            CHECK(changed != nullptr && changed->width == target.first && changed->height == target.second);
            Server::Capture capture;
            CHECK(screen->Grab(capture));
            CHECK(capture.width == target.first && capture.height == target.second);

            // A size nobody lists gets the nearest listed one, never an invented one.
            CHECK(screen->SetDisplayMode(id, 777, 555) == Wire::DisplayModeResult::Status::Ok);
            XGetWindowAttributes(x, root, &attributes);
            bool isListed = false;
            for (const auto& mode : listed)
            {
                isListed = isListed || (mode.first == attributes.width && mode.second == attributes.height);
            }
            printf("  asked for 777x555, got %dx%d\n", attributes.width, attributes.height);
            CHECK(isListed);
            // A display that isn't there, and nonsense sizes, change nothing.
            CHECK(screen->SetDisplayMode(0xFFFFFF, 800, 600) == Wire::DisplayModeResult::Status::NoSuchDisplay);
            CHECK(screen->SetDisplayMode(id, 0, 0) == Wire::DisplayModeResult::Status::NotListed);

            screen->RestoreDisplayModes();
            XGetWindowAttributes(x, root, &attributes);
            printf("  after restoring the desktop is %dx%d\n", attributes.width, attributes.height);
            CHECK(attributes.width == originalWidth && attributes.height == originalHeight);
        });

        RunTest("Linux: the pointer's shape is reported when it changes and its position follows", [x, root]() {
            std::string error;
            std::unique_ptr<Server::ScreenSource> screen = Server::CreateSystemScreen(error);
            CHECK(screen != nullptr);
            if (!screen)
            {
                return;
            }
            Server::Cursor cursor;
            CHECK(screen->GetCursor(cursor)); // the first call always reports
            CHECK(!screen->GetCursor(cursor)); // and then nothing until it changes

            Cursor hand = XCreateFontCursor(x, XC_hand2);
            XDefineCursor(x, root, hand);
            XSync(x, False);
            XWarpPointer(x, None, root, 0, 0, 0, 0, 40, 30);
            XSync(x, False);
            CHECK(WaitUntil([&]() { return screen->GetCursor(cursor); }));
            printf("  new cursor %dx%d hotspot %d,%d\n", cursor.width, cursor.height, cursor.hotspotX, cursor.hotspotY);
            CHECK(cursor.width > 0 && cursor.height > 0 && cursor.argb.size() ==
                                                                static_cast<size_t>(cursor.width) * cursor.height);
            bool opaque = std::any_of(cursor.argb.begin(), cursor.argb.end(), [](uint32_t p) { return (p >> 24) == 0xFF; });
            CHECK(opaque);
            CHECK(!screen->GetCursor(cursor));

            Cursor arrow = XCreateFontCursor(x, XC_left_ptr);
            XDefineCursor(x, root, arrow);
            XSync(x, False);
            Server::Cursor second;
            CHECK(WaitUntil([&]() { return screen->GetCursor(second); }));
            CHECK(second.argb != cursor.argb);

            int32_t px = 0;
            int32_t py = 0;
            CHECK(screen->GetCursorPosition(px, py));
            CHECK(px == 40 && py == 30);
            XWarpPointer(x, None, root, 0, 0, 0, 0, 123, 77);
            XSync(x, False);
            CHECK(screen->GetCursorPosition(px, py));
            CHECK(px == 123 && py == 77);
            XUndefineCursor(x, root);
            XFreeCursor(x, hand);
            XFreeCursor(x, arrow);
            XSync(x, False);
        });

        XCloseDisplay(x);
    }
} // namespace MarconiServerTest

#else

namespace MarconiServerTest
{
    void RunLinuxScreenTests() {}
} // namespace MarconiServerTest

#endif
