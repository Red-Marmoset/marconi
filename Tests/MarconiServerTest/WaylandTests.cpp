// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux Wayland capture and input, through the desktop portal. A private D-Bus daemon carries a fake portal (it
// answers like xdg-desktop-portal and records the keyboard and mouse calls); a fake screen publishes a real
// PipeWire video stream. Needs dbus-daemon for everything; the parts that read pictures also need a PipeWire daemon
// with a session manager (WirePlumber) running under XDG_RUNTIME_DIR. Without those, the tests print SKIP and pass.
// The real portal is reached only by the opt-in "live" tests at the end (see SERVER_PLAN.md, Wayland).

#include "WaylandTestSupport.h"

#if defined(__linux__)

#include <MarconiServerCore/CaptureStatus.h>
#include <MarconiServerCore/PipeWireCapture.h>
#include <MarconiServerCore/PipeWireLite.h>
#include <MarconiServerCore/SystemInput.h>
#include <MarconiServerCore/SystemScreen.h>
#include <MarconiServerCore/WaylandSupport.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;
    using namespace Marconi::Server;

    using namespace WaylandTestSupport;

    namespace
    {
        void PortalTests(PrivateBus& bus)
        {
            TimedTest("Wayland portal: the person's refusal is explained and says what to do", [&]() {
                FakePortalConfig config;
                config.approve = false;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                auto session = WaylandPortalSession::Start(OptionsFor(bus, TempFile("token")));
                CHECK(session->WaitUntilSettled(10000));
                CHECK(session->GetState() == WaylandPortalSession::State::Declined);
                const std::string text = session->GetStatusText();
                CHECK(Contains(text, "was not allowed"));
                CHECK(Contains(text, "Check again"));
                CHECK(Contains(text, "settings"));
                CHECK(!session->HasControl());
                auto calls = portal.Calls();
                CHECK(Has(calls, "RemoteDesktop.CreateSession") && Has(calls, "RemoteDesktop.SelectDevices") &&
                      Has(calls, "ScreenCast.SelectSources") && Has(calls, "RemoteDesktop.Start"));
                CHECK(!Has(calls, "ScreenCast.OpenPipeWireRemote")); // nothing opened after a no
                CHECK(Eventually([&]() { return portal.Calls().size() >= 4; }));
            });

            TimedTest("Wayland: when the person has said no, no screen is made and the input doesn't ask again", [&]() {
                FakePortalConfig config;
                config.approve = false;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                std::string screenError;
                auto source = CreateSystemScreen(screenError);
                CHECK(source == nullptr);
                CHECK(Contains(screenError, "was not allowed") && Contains(screenError, "Check again"));
                std::string inputError;
                auto input = CreateSystemInput(inputError);
                CHECK(input == nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                size_t starts = 0;
                for (const auto& call : portal.Calls())
                {
                    starts += call == "RemoteDesktop.Start" ? 1 : 0;
                }
                CHECK(starts == 1); // one question to the person, not two
                // "Check again" asks afresh.
                FakePortalConfig now;
                now.approve = false;
                portal.Configure(now);
                auto again = CreateSystemScreen(screenError);
                CHECK(again == nullptr);
                starts = 0;
                for (const auto& call : portal.Calls())
                {
                    starts += call == "RemoteDesktop.Start" ? 1 : 0;
                }
                CHECK(starts == 2);
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });

            TimedTest("Wayland portal: a failure of the desktop's own sharing service is not blamed on the person", [&]() {
                FakePortalConfig config;
                config.approve = false;
                config.declineCode = 2;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                auto session = WaylandPortalSession::Start(OptionsFor(bus, TempFile("token")));
                CHECK(session->WaitUntilSettled(10000));
                CHECK(session->GetState() == WaylandPortalSession::State::Failed);
                CHECK(Contains(session->GetStatusText(), "not an answer from you"));
                CHECK(Contains(session->GetStatusText(), "xdg-desktop-portal-wlr"));
            });

            TimedTest("Wayland portal: while the person is being asked the status says to allow it", [&]() {
                FakePortalConfig config;
                config.approve = false;
                config.answerDelayMs = 800;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                auto session = WaylandPortalSession::Start(OptionsFor(bus, TempFile("token")));
                CHECK(session->WaitUntilAnsweredOrAsking(5000));
                CHECK(session->GetState() == WaylandPortalSession::State::WaitingForApproval);
                CHECK(Contains(session->GetStatusText(), "waiting for you to allow screen sharing"));
                CHECK(Contains(session->GetStatusText(), "Share"));
                CHECK(session->WaitUntilSettled(10000));
                CHECK(session->GetState() == WaylandPortalSession::State::Declined);
            });

            TimedTest("Wayland portal: the desktop's remember-my-answer token is saved and offered next time", [&]() {
                FakePortalConfig config;
                config.node = 1; // no stream behind it: the session will fail at the video, after the token is saved
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                std::string tokenPath = TempFile("wayland-portal.token");
                {
                    auto first = WaylandPortalSession::Start(OptionsFor(bus, tokenPath));
                    CHECK(first->WaitUntilSettled(10000));
                }
                std::ifstream file(tokenPath);
                std::string saved;
                std::getline(file, saved);
                CHECK(saved == "fake-token-1");
                CHECK(portal.LastRestoreTokenSeen().empty()); // the first run had none to offer
                {
                    auto second = WaylandPortalSession::Start(OptionsFor(bus, tokenPath));
                    CHECK(second->WaitUntilSettled(10000));
                }
                CHECK(portal.LastRestoreTokenSeen() == "fake-token-1");
                std::ifstream again(tokenPath);
                std::string renewed;
                std::getline(again, renewed);
                CHECK(renewed == "fake-token-2"); // each answer's token replaces the last
            });
        }

        // Everything that needs a PipeWire daemon.
        void StreamTests(PrivateBus& bus)
        {
            TimedTest("Wayland capture: pictures from a PipeWire stream, with changed tiles, in every common layout", [&]() {
                const uint32_t formats[] = {Pw::kVideoBGRx, Pw::kVideoRGBA, Pw::kVideoARGB, Pw::kVideoBGR};
                for (uint32_t format : formats)
                {
                    FakeScreen screen;
                    std::string error;
                    CHECK(screen.Start(320, 200, format, error));
                    if (!error.empty())
                    {
                        printf("  (%s)\n", error.c_str());
                        return;
                    }
                    screen.Fill(kRed);
                    screen.FillRect(10, 10, 40, 30, kGreen);
                    PipeWireCapture capture;
                    CHECK(capture.Start(-1, screen.NodeId(), true, error));
                    std::vector<uint32_t> pixels;
                    int32_t width = 0;
                    int32_t height = 0;
                    uint64_t sequence = 0;
                    CHECK(Eventually([&]() { return capture.GetLatest(pixels, width, height, sequence); }));
                    CHECK(width == 320 && height == 200);
                    if (pixels.size() == 320u * 200u)
                    {
                        CHECK(pixels[0] == kRed);
                        CHECK(pixels[15 * 320 + 15] == kGreen);
                        CHECK(pixels[199 * 320 + 319] == kRed);
                    }
                    CHECK(capture.GetState() == PipeWireCapture::State::Streaming);
                }
            });

            TimedTest("Wayland capture: the screen source reports tiles that changed, and a new size", [&]() {
                FakeScreen screen;
                std::string error;
                CHECK(screen.Start(320, 200, Pw::kVideoBGRx, error));
                screen.Fill(kBlue);
                FakePortalConfig config;
                config.node = screen.NodeId();
                config.pipewireSocket = FindPipeWireSocket();
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);

                auto source = CreateSystemScreen(error);
                CHECK(source != nullptr);
                if (!source)
                {
                    printf("  (%s)\n", error.c_str());
                    return;
                }
                Capture capture;
                CHECK(Eventually([&]() { return source->Grab(capture); }));
                CHECK(capture.width == 320 && capture.height == 200);
                CHECK(capture.dirty.size() == 1 && capture.dirty[0].width == 320 && capture.dirty[0].height == 200);
                CHECK(capture.Row(5)[5] == kBlue);
                auto displays = source->GetDisplays();
                CHECK(displays.size() == 1 && displays[0].width == 320 && displays[0].height == 200);
                CHECK(GetCaptureStatus().empty()); // sharing: nothing to tell the person

                // Nothing changes: no dirty tiles (the stream keeps sending the same picture).
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                CHECK(source->Grab(capture));
                CHECK(capture.dirty.empty());

                // Paint inside one tile.
                screen.FillRect(70, 70, 10, 10, kGreen);
                bool seen = Eventually([&]() {
                    source->Grab(capture);
                    return !capture.dirty.empty();
                });
                CHECK(seen);
                if (seen)
                {
                    CHECK(capture.dirty.size() == 1);
                    CHECK(capture.dirty[0].x == 64 && capture.dirty[0].y == 64 && capture.dirty[0].width == 64 &&
                          capture.dirty[0].height == 64);
                    CHECK(capture.Row(75)[75] == kGreen);
                }

                // The display's size changes.
                screen.Resize(400, 300);
                screen.Fill(kRed);
                bool resized = Eventually([&]() { return source->Grab(capture) && capture.width == 400; });
                CHECK(resized);
                if (resized)
                {
                    CHECK(capture.height == 300);
                    CHECK(capture.dirty.size() == 1 && capture.dirty[0].width == 400 && capture.dirty[0].height == 300);
                    auto resizedDisplays = source->GetDisplays();
                    CHECK(resizedDisplays[0].width == 400 && resizedDisplays[0].height == 300);
                }
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });

            TimedTest("Wayland capture: the pointer's image and position come as separate data", [&]() {
                FakeScreen screen;
                std::string error;
                CHECK(screen.Start(320, 200, Pw::kVideoBGRx, error));
                screen.Fill(kBlue);
                screen.SetCursor(30, 40, 1, 2, 4, 3, 0xFFFF0000u); // an opaque red 4x3 pointer
                FakePortalConfig config;
                config.node = screen.NodeId();
                config.pipewireSocket = FindPipeWireSocket();
                config.cursorModes = 7;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                auto source = CreateSystemScreen(error);
                CHECK(source != nullptr);
                if (!source)
                {
                    return;
                }
                CHECK(portal.LastCursorModeSeen() == "4"); // asked for the pointer as metadata
                Capture capture;
                CHECK(Eventually([&]() { return source->Grab(capture); }));
                if (capture.width == 0)
                {
                    printf("  (no picture; state %d, status: %s)\n", static_cast<int>(AcquireWaylandPortal()->GetCapture().GetState()),
                           GetCaptureStatus().c_str());
                }
                Cursor cursor;
                CHECK(Eventually([&]() { return source->GetCursor(cursor); }));
                CHECK(cursor.width == 4 && cursor.height == 3 && cursor.hotspotX == 1 && cursor.hotspotY == 2);
                CHECK(cursor.argb.size() == 12 && cursor.argb[0] == 0xFFFF0000u);
                int32_t x = 0;
                int32_t y = 0;
                CHECK(source->GetCursorPosition(x, y) && x == 30 && y == 40);
                CHECK(!source->GetCursor(cursor)); // the same shape is not reported twice
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });

            TimedTest("Wayland input: pointer, buttons, wheel and keys reach the desktop's control calls", [&]() {
                FakeScreen screen;
                std::string error;
                CHECK(screen.Start(320, 200, Pw::kVideoBGRx, error));
                FakePortalConfig config;
                config.node = screen.NodeId();
                config.pipewireSocket = FindPipeWireSocket();
                config.streamWidth = 640; // the desktop counts in units twice the picture's pixels
                config.streamHeight = 400;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);

                auto source = CreateSystemScreen(error);
                auto input = CreateSystemInput(error);
                CHECK(source != nullptr && input != nullptr);
                if (!source || !input)
                {
                    return;
                }
                Capture capture;
                CHECK(Eventually([&]() { return source->Grab(capture); })); // the stream, and so the size, is known
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                input->MovePointer(100, 50, 0, 0, 0);
                CHECK(Eventually([&]() { return Has(portal.Input(), "motion 200.0 100.0 node " + std::to_string(screen.NodeId())); }));
                input->MovePointer(100, 50, 0x01, 0, 0); // left button down
                input->MovePointer(100, 50, 0x01 | 0x04, 0, 0); // right too
                input->MovePointer(100, 50, 0x04, 0, 0); // left up
                input->MovePointer(100, 50, 0x02, 0, 0); // middle down, right up
                CHECK(Eventually([&]() { return Has(portal.Input(), "button 274 1"); }));
                auto lines = portal.Input();
                CHECK(Has(lines, "button 272 1") && Has(lines, "button 273 1") && Has(lines, "button 272 0") &&
                      Has(lines, "button 273 0") && Has(lines, "button 274 1"));
                input->MovePointer(100, 50, 0, 120, 0);   // wheel up one notch
                input->MovePointer(100, 50, 0, -240, 0);  // down two notches
                input->MovePointer(100, 50, 0, 0, 120);   // right one notch
                CHECK(Eventually([&]() { return Has(portal.Input(), "axis 1 1"); }));
                lines = portal.Input();
                CHECK(Has(lines, "axis 0 -1")); // up is negative for the portal
                CHECK(Has(lines, "axis 0 2"));
                input->SendKey(0x61, 30, true);
                input->SendKey(0x61, 30, false);
                input->SendKey(0xFFE1, 0, true); // Shift_L
                input->TypeText("\xC3\xA9");     // e with an acute accent
                CHECK(Eventually([&]() { return Has(portal.Input(), "key 233 0"); }));
                lines = portal.Input();
                CHECK(Has(lines, "key 97 1") && Has(lines, "key 97 0") && Has(lines, "key 65505 1"));
                CHECK(Has(lines, "key 233 1"));
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });

            TimedTest("Wayland: a desktop with no keyboard and mouse control says so, and input is dropped", [&]() {
                FakeScreen screen;
                std::string error;
                CHECK(screen.Start(320, 200, Pw::kVideoBGRx, error));
                FakePortalConfig config;
                config.node = screen.NodeId();
                config.pipewireSocket = FindPipeWireSocket();
                config.offerRemoteDesktop = false;
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                auto source = CreateSystemScreen(error);
                auto input = CreateSystemInput(error);
                CHECK(source != nullptr && input != nullptr);
                if (!source || !input)
                {
                    return;
                }
                Capture capture;
                CHECK(Eventually([&]() { return source->Grab(capture); }));
                CHECK(Contains(GetCaptureStatus(), "can't control it"));
                CHECK(Contains(GetCaptureStatus(), "no keyboard and mouse control"));
                input->MovePointer(10, 10, 0x01, 0, 0);
                input->SendKey(0x61, 0, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                CHECK(portal.Input().empty());
                auto calls = portal.Calls();
                CHECK(Has(calls, "ScreenCast.CreateSession") && !Has(calls, "RemoteDesktop.CreateSession"));
                CHECK(portal.LastRestoreTokenSeen().empty());
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });

            TimedTest("Wayland: when the desktop ends the sharing the status says how to share again", [&]() {
                FakeScreen screen;
                std::string error;
                CHECK(screen.Start(320, 200, Pw::kVideoBGRx, error));
                FakePortalConfig config;
                config.node = screen.NodeId();
                config.pipewireSocket = FindPipeWireSocket();
                FakePortal portal(config);
                CHECK(portal.Start(bus.Address()));
                SetWaylandPortalOptions(OptionsFor(bus, TempFile("token")));
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                auto source = CreateSystemScreen(error);
                CHECK(source != nullptr);
                if (!source)
                {
                    return;
                }
                Capture capture;
                CHECK(Eventually([&]() { return source->Grab(capture); }));
                CHECK(GetCaptureStatus().empty());
                portal.EndSession();
                CHECK(Eventually([&]() { return !GetCaptureStatus().empty(); }));
                CHECK(Contains(GetCaptureStatus(), "stopped sharing"));
                CHECK(Contains(GetCaptureStatus(), "Check again"));
                source.reset();
                CHECK(Eventually([&]() { return GetCaptureStatus().empty(); })); // letting go ends the session
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });
        }

        // Against a real desktop: set MARCONI_WAYLAND_LIVE=1 (and, for the picture check, MARCONI_WAYLAND_LIVE_COLOUR
        // to the hex colour the whole screen is painted, for example ff0000) in a session whose D-Bus and portal are
        // running (Tests/MarconiServerTest/wayland/RunLiveSwayTest.sh starts a headless Sway for it). Uses the real
        // session bus and the real portal, so on a desktop that asks the person, the person must answer.
        void LiveTest()
        {
            const char* live = std::getenv("MARCONI_WAYLAND_LIVE");
            if (live == nullptr || std::string(live) != "1")
            {
                return;
            }
            TimedTest("Wayland live: the real desktop portal and PipeWire give the screen", []() {
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                WaylandPortalOptions options;
                options.tokenPath = TempFile("live-token");
                SetWaylandPortalOptions(options);
                std::string error;
                auto source = CreateSystemScreen(error);
                auto input = CreateSystemInput(error);
                CHECK(source != nullptr && input != nullptr);
                if (!source)
                {
                    printf("  (%s)\n", error.c_str());
                    return;
                }
                Capture capture;
                bool got = Eventually([&]() { return source->Grab(capture); }, 60000);
                printf("  live status: \"%s\"\n", GetCaptureStatus().c_str());
                CHECK(got);
                if (!got)
                {
                    return;
                }
                printf("  live picture: %dx%d, pixel at the centre %06X, top left %06X\n", capture.width, capture.height,
                       capture.Row(capture.height / 2)[capture.width / 2], capture.Row(0)[0]);
                const char* colour = std::getenv("MARCONI_WAYLAND_LIVE_COLOUR");
                if (colour != nullptr)
                {
                    uint32_t expected = static_cast<uint32_t>(std::strtoul(colour, nullptr, 16));
                    CHECK(capture.Row(capture.height / 2)[capture.width / 2] == expected);
                    CHECK(capture.Row(0)[0] == expected);
                    CHECK(capture.Row(capture.height - 1)[capture.width - 1] == expected);
                }
                Cursor cursor;
                source->GetCursor(cursor);
                printf("  live pointer image: %dx%d\n", cursor.width, cursor.height);
                unsetenv("MARCONI_WAYLAND_PORTAL");
            });
        }

        // Against a real compositor's own stream, without the portal: MARCONI_PIPEWIRE_NODE is the PipeWire node a
        // compositor (mutter's own ScreenCast interface, for example) publishes, and MARCONI_PIPEWIRE_COLOUR the hex
        // colour its screen is painted. Tools/wayland/RunMutterCaptureCheck.sh sets both up.
        void LiveNodeTest()
        {
            const char* node = std::getenv("MARCONI_PIPEWIRE_NODE");
            if (node == nullptr || *node == '\0')
            {
                return;
            }
            TimedTest("Wayland live: a real compositor's PipeWire stream is read", [&]() {
                PipeWireCapture capture;
                std::string error;
                CHECK(capture.Start(-1, static_cast<uint32_t>(std::strtoul(node, nullptr, 10)), true, error));
                std::vector<uint32_t> pixels;
                int32_t width = 0;
                int32_t height = 0;
                uint64_t sequence = 0;
                bool got = Eventually([&]() { return capture.GetLatest(pixels, width, height, sequence); }, 20000);
                CHECK(got);
                if (!got)
                {
                    printf("  (state %d: %s)\n", static_cast<int>(capture.GetState()), capture.GetFailure().c_str());
                    return;
                }
                printf("  live stream: %dx%d, centre %06X, top left %06X\n", width, height,
                       pixels[static_cast<size_t>(height / 2) * width + width / 2], pixels[0]);
                const char* colour = std::getenv("MARCONI_PIPEWIRE_COLOUR");
                if (colour != nullptr)
                {
                    uint32_t expected = static_cast<uint32_t>(std::strtoul(colour, nullptr, 16));
                    CHECK(pixels[static_cast<size_t>(height / 2) * width + width / 2] == expected);
                    CHECK(pixels[0] == expected);
                    CHECK(pixels.back() == expected);
                }
            });
        }
    } // namespace

    void RunWaylandTests()
    {
        RunWaylandPureTests();

        PrivateBus bus;
        if (!bus.Start())
        {
            printf("SKIP Wayland portal tests: dbus-daemon isn't installed\n");
            return;
        }
        std::string error;
        if (!DBusLink::IsAvailable(error))
        {
            printf("SKIP Wayland portal tests: %s\n", error.c_str());
            return;
        }
        RunWaylandBusTests(bus);
        PortalTests(bus);

        const std::string socket = FindPipeWireSocket();
        if (socket.empty() || Pw::GetApi(error) == nullptr)
        {
            printf("SKIP Wayland capture and input tests: no PipeWire daemon (start pipewire and wireplumber with "
                   "XDG_RUNTIME_DIR set) or no libpipewire\n");
            return;
        }
        StreamTests(bus);
        LiveNodeTest();
        LiveTest();
    }
} // namespace MarconiServerTest

#else

namespace MarconiServerTest
{
    void RunWaylandTests() {}
} // namespace MarconiServerTest

#endif
