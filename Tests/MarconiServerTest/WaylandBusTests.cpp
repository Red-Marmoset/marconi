// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux Wayland: the parts that need no PipeWire. Picture format conversion, reading negotiated formats, which
// sessions use the desktop portal, and D-Bus values making the round trip through a real (private) bus. See
// WaylandTests.cpp for the portal and stream tests.

#include "WaylandTestSupport.h"

#if defined(__linux__)

#include <MarconiServerCore/PipeWireCapture.h>
#include <MarconiServerCore/PipeWireLite.h>
#include <MarconiServerCore/WaylandSupport.h>

#include <atomic>
#include <cstdlib>

namespace MarconiServerTest
{
    using namespace WaylandTestSupport;

    namespace
    {
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
    } // namespace

    void RunWaylandPureTests() { PureTests(); }

    void RunWaylandBusTests(PrivateBus& bus) { BusTests(bus); }
} // namespace MarconiServerTest

#else

namespace MarconiServerTest
{
} // namespace MarconiServerTest

#endif
