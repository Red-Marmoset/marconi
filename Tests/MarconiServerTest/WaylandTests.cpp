// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux Wayland capture and input, through the desktop portal. A private D-Bus daemon carries a fake portal (it
// answers like xdg-desktop-portal and records the keyboard and mouse calls); a fake screen publishes a real
// PipeWire video stream. Needs dbus-daemon for everything; the parts that read pictures also need a PipeWire daemon
// with a session manager (WirePlumber) running under XDG_RUNTIME_DIR. Without those, the tests print SKIP and pass.
// A real compositor and portal are not part of these tests (see SERVER_PLAN.md, Wayland).

#include "TestSupport.h"

#if defined(__linux__)

#include "WaylandFakes.h"

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

    namespace
    {
        constexpr uint32_t kRed = 0xFF0000;
        constexpr uint32_t kGreen = 0x00FF00;
        constexpr uint32_t kBlue = 0x0000FF;

        std::string TempFile(const char* name)
        {
            static int counter = 0;
            std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                        ("marconi-wayland-test-" + std::to_string(getpid()) + "-" + std::to_string(++counter));
            std::filesystem::create_directories(dir);
            return (dir / name).string();
        }

        WaylandPortalOptions OptionsFor(const PrivateBus& bus, const std::string& tokenPath)
        {
            WaylandPortalOptions options;
            options.busAddress = bus.Address();
            options.tokenPath = tokenPath;
            options.approvalTimeoutMs = 10000;
            return options;
        }

        bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

        bool Has(const std::vector<std::string>& lines, const std::string& line)
        {
            for (const auto& entry : lines)
            {
                if (entry == line)
                {
                    return true;
                }
            }
            return false;
        }

        // Waits (up to `ms`) for `condition` to hold.
        template <typename F>
        bool Eventually(F condition, int ms = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (condition())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return condition();
        }

        // Runs a test and says so when it was slow, since a hang in a portal or PipeWire call shows up as time.
        void TimedTest(const char* name, const std::function<void()>& body)
        {
            auto start = std::chrono::steady_clock::now();
            RunTest(name, body);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (ms > 3000)
            {
                printf("  (slow: %lld ms)\n", static_cast<long long>(ms));
            }
        }

        void PureTests()
        {
            TimedTest("Wayland: picture formats are converted to 0x00RRGGBB", [&]() {
                // One pixel of each layout holding red 0x11, green 0x22, blue 0x33.
                const uint32_t expected = 0x112233;
                struct Case
                {
                    uint32_t format;
                    std::vector<uint8_t> bytes;
                };
                const Case cases[] = {
                    {Pw::kVideoBGRx, {0x33, 0x22, 0x11, 0xFF}}, {Pw::kVideoBGRA, {0x33, 0x22, 0x11, 0x80}},
                    {Pw::kVideoRGBx, {0x11, 0x22, 0x33, 0xFF}}, {Pw::kVideoRGBA, {0x11, 0x22, 0x33, 0x80}},
                    {Pw::kVideoxRGB, {0xFF, 0x11, 0x22, 0x33}}, {Pw::kVideoARGB, {0x80, 0x11, 0x22, 0x33}},
                    {Pw::kVideoxBGR, {0xFF, 0x33, 0x22, 0x11}}, {Pw::kVideoABGR, {0x80, 0x33, 0x22, 0x11}},
                    {Pw::kVideoRGB, {0x11, 0x22, 0x33}},        {Pw::kVideoBGR, {0x33, 0x22, 0x11}},
                };
                for (const Case& c : cases)
                {
                    std::vector<uint32_t> out;
                    CHECK(PipeWireCapture::ConvertRows(c.format, c.bytes.data(), static_cast<int32_t>(c.bytes.size()), 1,
                                                       1, out));
                    CHECK(out.size() == 1 && out[0] == expected);
                }
                // Two rows with padding between them.
                std::vector<uint8_t> padded = {0x33, 0x22, 0x11, 0, 1, 2, 3, 4, 0x66, 0x55, 0x44, 0, 5, 6, 7, 8};
                std::vector<uint32_t> out;
                CHECK(PipeWireCapture::ConvertRows(Pw::kVideoBGRx, padded.data(), 8, 1, 2, out));
                CHECK(out.size() == 2 && out[0] == 0x112233 && out[1] == 0x445566);
                CHECK(!PipeWireCapture::ConvertRows(9999, padded.data(), 8, 1, 2, out)); // unknown format
            });

            TimedTest("Wayland: a negotiated format is read, whether plain or wrapped in single-value choices", [&]() {
                Pw::PodBuilder plain;
                plain.BeginObject(Pw::kObjectFormat, Pw::kParamFormat);
                plain.AddId(Pw::kFormatMediaType, Pw::kMediaTypeVideo);
                plain.AddId(Pw::kFormatMediaSubtype, Pw::kMediaSubtypeRaw);
                plain.AddId(Pw::kFormatVideoFormat, Pw::kVideoRGBA);
                plain.AddRectangle(Pw::kFormatVideoSize, 800, 600);
                plain.AddFraction(Pw::kFormatVideoFramerate, 30, 1);
                Pw::VideoFormat format;
                CHECK(Pw::ParseVideoFormat(plain.Finish(), format));
                CHECK(format.format == Pw::kVideoRGBA && format.width == 800 && format.height == 600);
                CHECK(format.framerateNum == 30 && !format.hasModifier);

                Pw::PodBuilder choice;
                choice.BeginObject(Pw::kObjectFormat, Pw::kParamFormat);
                choice.AddId(Pw::kFormatMediaType, Pw::kMediaTypeVideo);
                choice.AddId(Pw::kFormatMediaSubtype, Pw::kMediaSubtypeRaw);
                choice.AddIdChoiceEnum(Pw::kFormatVideoFormat, Pw::kVideoBGRx, {Pw::kVideoBGRx, Pw::kVideoRGBA});
                choice.AddRectangleChoiceRange(Pw::kFormatVideoSize, 1920, 1080, 1, 1, 4096, 4096);
                Pw::VideoFormat fromChoice;
                CHECK(Pw::ParseVideoFormat(choice.Finish(), fromChoice));
                CHECK(fromChoice.format == Pw::kVideoBGRx && fromChoice.width == 1920 && fromChoice.height == 1080);

                Pw::PodBuilder audio;
                audio.BeginObject(Pw::kObjectFormat, Pw::kParamFormat);
                audio.AddId(Pw::kFormatMediaType, 1);
                audio.AddId(Pw::kFormatMediaSubtype, Pw::kMediaSubtypeRaw);
                Pw::VideoFormat none;
                CHECK(!Pw::ParseVideoFormat(audio.Finish(), none)); // not video
                CHECK(!Pw::ParseVideoFormat(nullptr, none));
            });

            TimedTest("Wayland: the session type decides whether the desktop portal is used", [&]() {
                const char* oldForce = std::getenv("MARCONI_WAYLAND_PORTAL");
                const char* oldType = std::getenv("XDG_SESSION_TYPE");
                const char* oldDisplay = std::getenv("DISPLAY");
                const char* oldWayland = std::getenv("WAYLAND_DISPLAY");
                std::string savedForce = oldForce != nullptr ? oldForce : "";
                std::string savedType = oldType != nullptr ? oldType : "";
                std::string savedDisplay = oldDisplay != nullptr ? oldDisplay : "";
                std::string savedWayland = oldWayland != nullptr ? oldWayland : "";
                bool hadForce = oldForce != nullptr, hadType = oldType != nullptr, hadDisplay = oldDisplay != nullptr,
                     hadWayland = oldWayland != nullptr;

                unsetenv("MARCONI_WAYLAND_PORTAL");
                unsetenv("XDG_SESSION_TYPE");
                unsetenv("WAYLAND_DISPLAY");
                setenv("DISPLAY", ":0", 1);
                CHECK(!ShouldUseWaylandPortal()); // X11
                setenv("XDG_SESSION_TYPE", "wayland", 1);
                CHECK(ShouldUseWaylandPortal()); // a Wayland session, even with XWayland's DISPLAY
                setenv("XDG_SESSION_TYPE", "x11", 1);
                CHECK(!ShouldUseWaylandPortal());
                unsetenv("DISPLAY");
                setenv("WAYLAND_DISPLAY", "wayland-0", 1);
                CHECK(ShouldUseWaylandPortal()); // only a Wayland display to talk to
                setenv("MARCONI_WAYLAND_PORTAL", "0", 1);
                CHECK(!ShouldUseWaylandPortal());
                setenv("DISPLAY", ":0", 1);
                unsetenv("WAYLAND_DISPLAY");
                setenv("MARCONI_WAYLAND_PORTAL", "1", 1);
                CHECK(ShouldUseWaylandPortal());

                auto restore = [](const char* name, bool had, const std::string& value) {
                    if (had)
                    {
                        setenv(name, value.c_str(), 1);
                    }
                    else
                    {
                        unsetenv(name);
                    }
                };
                restore("MARCONI_WAYLAND_PORTAL", hadForce, savedForce);
                restore("XDG_SESSION_TYPE", hadType, savedType);
                restore("DISPLAY", hadDisplay, savedDisplay);
                restore("WAYLAND_DISPLAY", hadWayland, savedWayland);
            });
        }

        void BusTests(PrivateBus& bus)
        {
            TimedTest("Wayland D-Bus: values of every shape make the round trip through a bus", [&]() {
                std::string error;
                auto server = DBusLink::Connect(bus.Address(), error);
                auto client = DBusLink::Connect(bus.Address(), error);
                CHECK(server != nullptr && client != nullptr);
                if (!server || !client)
                {
                    printf("  (%s)\n", error.c_str());
                    return;
                }
                CHECK(server->RequestName("org.marconi.EchoTest", error));
                std::atomic<bool> stop{false};
                std::thread echo([&]() {
                    while (!stop.load())
                    {
                        DBusMessageIn call;
                        if (server->Receive(call, 50) && call.type == DBusMessageIn::Type::Call)
                        {
                            if (call.member == "Fail")
                            {
                                server->ReplyError(call, "org.marconi.Error.Nope", "not today");
                            }
                            else
                            {
                                server->Reply(call, call.args);
                            }
                        }
                    }
                });

                DBusValue options = DBusValue::Dict();
                options.Add("name", DBusValue::String("héllo"));
                options.Add("count", DBusValue::Uint(7));
                options.Add("pair", DBusValue::StructOf({DBusValue::Int(-3), DBusValue::Int(4)}));
                options.Add("yes", DBusValue::Bool(true));
                options.Add("big", DBusValue::ULong(0x1122334455667788ull));
                options.Add("half", DBusValue::Double(0.5));
                DBusValue streams;
                streams.kind = DBusValue::Kind::Array;
                streams.elementType = "(ua{sv})";
                DBusValue inner = DBusValue::Dict();
                inner.Add("id", DBusValue::String("s1"));
                streams.items.push_back(DBusValue::StructOf({DBusValue::Uint(42), inner}));

                std::vector<DBusValue> reply;
                std::string errorName;
                CHECK(client->Call("org.marconi.EchoTest", "/echo", "org.marconi.Echo", "Echo",
                                   {DBusValue::Path("/a/b"), options, streams}, reply, errorName, error, 5000));
                CHECK(reply.size() == 3);
                if (reply.size() == 3)
                {
                    CHECK(reply[0].kind == DBusValue::Kind::ObjectPath && reply[0].text == "/a/b");
                    CHECK(reply[1].Signature() == "a{sv}");
                    const DBusValue* name = reply[1].Find("name");
                    CHECK(name != nullptr && name->text == "héllo");
                    const DBusValue* count = reply[1].Find("count");
                    CHECK(count != nullptr && count->AsUint() == 7);
                    const DBusValue* pair = reply[1].Find("pair");
                    CHECK(pair != nullptr && pair->items.size() == 2 && pair->items[0].integer == -3 &&
                          pair->items[1].integer == 4);
                    const DBusValue* yes = reply[1].Find("yes");
                    CHECK(yes != nullptr && yes->integer == 1);
                    const DBusValue* big = reply[1].Find("big");
                    CHECK(big != nullptr && static_cast<uint64_t>(big->integer) == 0x1122334455667788ull);
                    const DBusValue* half = reply[1].Find("half");
                    CHECK(half != nullptr && half->real == 0.5);
                    CHECK(reply[1].Find("missing") == nullptr);
                    CHECK(reply[2].Signature() == "a(ua{sv})" && reply[2].items.size() == 1);
                    if (reply[2].items.size() == 1)
                    {
                        CHECK(reply[2].items[0].items[0].AsUint() == 42);
                        const DBusValue* id = reply[2].items[0].items[1].Find("id");
                        CHECK(id != nullptr && id->text == "s1");
                    }
                }

                // An empty dictionary keeps its type.
                reply.clear();
                CHECK(client->Call("org.marconi.EchoTest", "/echo", "org.marconi.Echo", "Echo", {DBusValue::Dict()},
                                   reply, errorName, error, 5000));
                CHECK(reply.size() == 1 && reply[0].items.empty());

                // An error reply gives the error's name and text.
                CHECK(!client->Call("org.marconi.EchoTest", "/echo", "org.marconi.Echo", "Fail", {}, reply, errorName,
                                    error, 5000));
                CHECK(errorName == "org.marconi.Error.Nope" && error == "not today");
                // A name nobody owns.
                CHECK(!client->Call("org.marconi.Nobody", "/x", "org.marconi.X", "Y", {}, reply, errorName, error, 2000));
                CHECK(!errorName.empty());

                // A signal reaches a link that asked for it.
                CHECK(client->AddMatch("type='signal',interface='org.marconi.Echo',member='Ping'", error));
                server->EmitSignal("/echo", "org.marconi.Echo", "Ping", {DBusValue::Uint(9)});
                DBusMessageIn signal;
                bool got = false;
                for (int i = 0; i < 20 && !got; ++i)
                {
                    got = client->Receive(signal, 100) && signal.member == "Ping";
                }
                CHECK(got && signal.args.size() == 1 && signal.args[0].AsUint() == 9);

                stop.store(true);
                echo.join();
            });

            TimedTest("Wayland: no desktop message bus is explained in plain words", [&]() {
                std::string dir = TempFile("token");
                WaylandPortalOptions options;
                options.busAddress = "unix:path=/nonexistent/marconi-no-bus";
                options.tokenPath = dir;
                auto session = WaylandPortalSession::Start(options);
                CHECK(session->WaitUntilSettled(5000));
                CHECK(session->GetState() == WaylandPortalSession::State::Failed);
                CHECK(Contains(session->GetStatusText(), "can't ask the desktop"));
            });

            TimedTest("Wayland: a desktop with no screen-sharing service is explained in plain words", [&]() {
                auto session = WaylandPortalSession::Start(OptionsFor(bus, TempFile("token")));
                CHECK(session->WaitUntilSettled(10000));
                CHECK(session->GetState() == WaylandPortalSession::State::Failed);
                CHECK(Contains(session->GetStatusText(), "xdg-desktop-portal"));
                CHECK(Contains(session->GetStatusText(), "screen-sharing service"));
            });
        }

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
        PureTests();

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
        BusTests(bus);
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
