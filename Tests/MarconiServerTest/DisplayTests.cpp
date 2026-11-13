// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Several displays, choosing one, matching the other computer's screen to the window, and the pointer: the real
// Wire client against the real server, with a scripted fake screen standing in for the computer's

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <algorithm>
#include <chrono>
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

        constexpr uint32_t kLeftColour = 0x00CC2222;
        constexpr uint32_t kRightColour = 0x002222CC;
        constexpr uint32_t kMarker = 0x0000FF00;

        // A computer with scripted displays, pointer and mode changes. Everything the test changes from its own
        // thread goes through the mutex, since the session reads it from its frame thread.
        class FakeScreen : public Server::ScreenSource
        {
        public:
            explicit FakeScreen(std::vector<Wire::Display> displays) : m_displays(std::move(displays)) {}

            std::vector<Wire::Display> GetDisplays() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_displays;
            }

            bool Grab(Server::Capture& out) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int32_t width = 0;
                int32_t height = 0;
                for (const Wire::Display& d : m_displays)
                {
                    width = std::max(width, d.x + d.width);
                    height = std::max(height, d.y + d.height);
                }
                out.width = width;
                out.height = height;
                out.pixels.assign(static_cast<size_t>(width) * height, 0);
                for (size_t i = 0; i < m_displays.size(); ++i)
                {
                    const Wire::Display& d = m_displays[i];
                    for (int32_t y = 0; y < d.height; ++y)
                    {
                        for (int32_t x = 0; x < d.width; ++x)
                        {
                            out.pixels[static_cast<size_t>(d.y + y) * width + d.x + x] =
                                i == 0 ? kLeftColour : kRightColour;
                        }
                    }
                    if (m_markers[i % 2])
                    {
                        out.pixels[static_cast<size_t>(d.y) * width + d.x] = kMarker;
                    }
                }
                out.dirty.clear();
                bool sizeChanged = width != m_lastWidth || height != m_lastHeight;
                if (sizeChanged)
                {
                    out.dirty.push_back({0, 0, width, height});
                }
                for (size_t i = 0; i < m_displays.size() && !sizeChanged; ++i)
                {
                    if (m_markers[i % 2] != m_drawnMarkers[i % 2])
                    {
                        const Wire::Display& d = m_displays[i];
                        out.dirty.push_back({d.x, d.y, 1, 1});
                    }
                }
                m_lastWidth = width;
                m_lastHeight = height;
                m_drawnMarkers[0] = m_markers[0];
                m_drawnMarkers[1] = m_markers[1];
                return true;
            }

            bool GetCursor(Server::Cursor& out) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_cursorChanged)
                {
                    return false;
                }
                m_cursorChanged = false;
                out = m_cursor;
                return true;
            }

            bool GetCursorPosition(int32_t& x, int32_t& y) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_positionReads;
                x = m_x;
                y = m_y;
                return true;
            }

            bool CanSetDisplayMode() override { return m_canResize; }

            // Only the sizes in m_modes are listed; the first display takes whichever listed size is exact.
            Wire::DisplayModeResult::Status SetDisplayMode(uint32_t displayId, int32_t width, int32_t height) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (Wire::Display& d : m_displays)
                {
                    if (d.id != displayId)
                    {
                        continue;
                    }
                    for (const auto& mode : m_modes)
                    {
                        if (mode.first == width && mode.second == height)
                        {
                            d.width = width;
                            d.height = height;
                            m_modeRequests.push_back(displayId);
                            return Wire::DisplayModeResult::Status::Ok;
                        }
                    }
                    return Wire::DisplayModeResult::Status::NotListed;
                }
                return Wire::DisplayModeResult::Status::NoSuchDisplay;
            }

            void RestoreDisplayModes() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_restores;
            }

            void SetMarker(size_t display, bool on)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_markers[display] = on;
            }

            void SetCursor(int32_t size, uint32_t colour, bool hidden = false)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cursor = Server::Cursor();
                if (!hidden)
                {
                    m_cursor.width = size;
                    m_cursor.height = size;
                    m_cursor.hotspotX = 1;
                    m_cursor.hotspotY = 2;
                    m_cursor.argb.assign(static_cast<size_t>(size) * size, 0xFF000000u | colour);
                }
                m_cursorChanged = true;
            }

            void MovePointer(int32_t x, int32_t y)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_x = x;
                m_y = y;
            }

            int Restores()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_restores;
            }

            size_t ModeRequests()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_modeRequests.size();
            }

            bool m_canResize = true;
            std::vector<std::pair<int32_t, int32_t>> m_modes{{64, 48}, {32, 24}};

        private:
            std::mutex m_mutex;
            std::vector<Wire::Display> m_displays;
            bool m_markers[2] = {false, false};
            bool m_drawnMarkers[2] = {false, false};
            int32_t m_lastWidth = 0;
            int32_t m_lastHeight = 0;
            Server::Cursor m_cursor;
            bool m_cursorChanged = true;
            int32_t m_x = 0;
            int32_t m_y = 0;
            int m_positionReads = 0;
            int m_restores = 0;
            std::vector<uint32_t> m_modeRequests;
        };

        // The recording listener plus what these tests watch: the pointer, resize refusals and desktop resizes.
        class DisplayListener : public RecordingClientListener
        {
        public:
            void OnCursorShape(const Marconi::CursorShape& shape) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                shapes.push_back(shape);
                changed.notify_all();
            }
            void OnCursorPosition(int x, int y) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++positions;
                lastX = x;
                lastY = y;
                changed.notify_all();
            }
            void OnDesktopResizeRefused(int status) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                refusals.push_back(status);
                changed.notify_all();
            }
            void OnDesktopResized(int width, int height) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                resizedWidth = width;
                resizedHeight = height;
                changed.notify_all();
            }

            std::vector<Marconi::CursorShape> shapes;
            int positions = 0;
            int lastX = -1;
            int lastY = -1;
            std::vector<int> refusals;
            int resizedWidth = 0;
            int resizedHeight = 0;
        };

        // A server on the loopback serving one viewer, and a connected client, for the tests below.
        struct Rig
        {
            explicit Rig(FakeScreen& screen) : listener(screen, input, MakeSettings()), client(clientListener)
            {
                listener.SetFrameIntervalMs(1);
                std::string error;
                started = listener.Start(error, "127.0.0.1");
                serverThread = std::thread([this]() { listener.ServeOne(5000); });
                Wire::ClientOptions options;
                options.host = "127.0.0.1";
                options.port = listener.GetPort();
                options.clientName = "display tests";
                client.Connect(options);
            }

            ~Rig()
            {
                client.Disconnect();
                listener.Stop();
                serverThread.join();
            }

            static Server::Settings MakeSettings()
            {
                Server::Settings settings;
                settings.allowNoPassword = true; // these tests log in without a password
                settings.port = 0;
                settings.serverName = "Display PC";
                settings.serverVersion = "0.0.0.0";
                return settings;
            }

            uint32_t Pixel(int x, int y)
            {
                Rfb::Framebuffer& fb = client.GetFramebuffer();
                std::lock_guard<std::mutex> lock(fb.GetMutex());
                return fb.GetPixel(x, y);
            }

            bool Connected() { return clientListener.WaitFor([&]() { return clientListener.connected; }); }

            RecordingInput input;
            Server::Listener listener;
            DisplayListener clientListener;
            Wire::Client client;
            std::thread serverThread;
            bool started = false;
        };
    } // namespace

    void RunDisplayTests()
    {
        RunTest("The viewer is told about every display and gets the whole desktop", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}, {20, 64, 0, 64, 48}});
            Rig rig(screen);
            CHECK(rig.started);
            CHECK(rig.Connected());
            CHECK(rig.clientListener.serverInit.width == 128 && rig.clientListener.serverInit.height == 48);
            std::vector<Rfb::Screen> screens = rig.client.GetScreens();
            CHECK(screens.size() == 2);
            CHECK(screens.size() == 2 && screens[0].id == 10 && screens[1].id == 20 && screens[1].x == 64 &&
                  screens[1].width == 64);
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.updates >= 1; }));
            CHECK(rig.Pixel(5, 5) == kLeftColour);
            CHECK(rig.Pixel(100, 5) == kRightColour);
        });

        RunTest("Choosing a display sends only that display, and All Displays brings the rest back", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}, {20, 64, 0, 64, 48}});
            Rig rig(screen);
            CHECK(rig.Connected());
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.updates >= 1; }));

            // The server applies the choice on its own frame thread and then sends the chosen display again whole: wait
            // for that picture before changing the screen, or a frame taken in between would still carry both.
            const int before = rig.clientListener.updates;
            rig.client.SelectDisplay(20);
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.updates > before; }));
            // A marker on the chosen display arrives, one on the other display does not.
            screen.SetMarker(0, true);
            screen.SetMarker(1, true);
            CHECK(WaitUntil([&]() { return rig.Pixel(64, 0) == kMarker; }));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CHECK(rig.Pixel(0, 0) != kMarker);

            // Back to everything: the whole desktop is sent again, so the left display catches up.
            rig.client.SelectDisplay(0);
            CHECK(WaitUntil([&]() { return rig.Pixel(0, 0) == kMarker; }));
        });

        RunTest("A display the server doesn't have is ignored", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}, {20, 64, 0, 64, 48}});
            Rig rig(screen);
            CHECK(rig.Connected());
            rig.client.SelectDisplay(999);
            screen.SetMarker(0, true);
            CHECK(WaitUntil([&]() { return rig.Pixel(0, 0) == kMarker; })); // still the whole desktop
        });

        RunTest("Matching the screen to the window changes the display's size and the viewer follows", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}});
            Rig rig(screen);
            CHECK(rig.Connected());
            CHECK(rig.client.SupportsDesktopResize());
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.updates >= 1; }));

            rig.client.RequestDesktopSize(32, 24);
            CHECK(rig.clientListener.WaitFor(
                [&]() { return rig.clientListener.resizedWidth == 32 && rig.clientListener.resizedHeight == 24; }));
            CHECK(rig.client.GetScreens().size() == 1 && rig.client.GetScreens()[0].width == 32);
            // The picture of the new size arrives and fills the smaller desktop.
            CHECK(WaitUntil([&]() { return rig.Pixel(31, 23) == kLeftColour; }));
            CHECK(screen.Restores() == 0); // not restored while the viewer is still there

            // A size the display doesn't list is refused and nothing changes.
            rig.client.RequestDesktopSize(50, 50);
            CHECK(rig.clientListener.WaitFor([&]() { return !rig.clientListener.refusals.empty(); }));
            CHECK(rig.clientListener.refusals[0] == 3);
            CHECK(screen.ModeRequests() == 1);
            CHECK(rig.client.GetScreens()[0].width == 32);

            // The viewer leaving puts the screen back.
            rig.client.Disconnect();
            CHECK(WaitUntil([&]() { return screen.Restores() >= 1; }));
        });

        RunTest("A server that can't change its screen size doesn't offer to", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}});
            screen.m_canResize = false;
            Rig rig(screen);
            CHECK(rig.Connected());
            CHECK(!rig.client.SupportsDesktopResize());
            rig.client.RequestDesktopSize(32, 24); // sends nothing
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK(screen.ModeRequests() == 0);
        });

        RunTest("A size change a viewer asks for without the capability is refused", []() {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);
            Server::TestPatternScreen screen(4, 3); // can't change its size
            RecordingInput input;
            Server::Settings settings;
            settings.allowNoPassword = true; // these tests log in without a password
            Server::Session session(serverTransport, screen, input, settings);
            session.SetFrameIntervalMs(1);
            std::thread serverThread([&]() { session.Run(); });

            Wire::MessageReader reader(clientTransport);
            auto send = [&](const Wire::Bytes& framed) { clientTransport.Write(framed.data(), framed.size()); };
            Wire::Hello hello;
            hello.capabilities = Wire::Capability::MultipleDisplays | Wire::Capability::ResizeDisplay;
            send(hello.Encode());
            Wire::Bytes bytes = reader.Next();
            Wire::Reader body(bytes);
            CHECK(Wire::ReadType(body) == Wire::Type::Welcome);
            CHECK((Wire::Welcome::Decode(body).capabilities & Wire::Capability::ResizeDisplay) == 0);
            send(Wire::Login{""}.Encode());

            Wire::SetDisplayMode mode;
            mode.displayId = 1;
            mode.width = 2;
            mode.height = 2;
            send(mode.Encode());
            bool refused = false;
            for (int i = 0; i < 100000 && !refused; ++i)
            {
                bytes = reader.Next();
                Wire::Reader message(bytes);
                if (Wire::ReadType(message) == Wire::Type::DisplayModeResult)
                {
                    refused = Wire::DisplayModeResult::Decode(message).status ==
                              Wire::DisplayModeResult::Status::NotAllowed;
                    break;
                }
            }
            CHECK(refused);
            send(Wire::Bye{}.Encode());
            clientToServer.Close();
            serverThread.join();
        });

        RunTest("The pointer's shape is sent when it changes, and its position when it moves", []() {
            FakeScreen screen({{10, 0, 0, 64, 48}});
            Rig rig(screen);
            CHECK(rig.Connected());
            // The first shape goes out at once.
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.shapes.size() >= 1; }));
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.positions >= 1; }));

            // Nothing changed for a while: no more shapes, no more positions.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            size_t shapesBefore;
            int positionsBefore;
            {
                std::lock_guard<std::mutex> lock(rig.clientListener.mutex);
                shapesBefore = rig.clientListener.shapes.size();
                positionsBefore = rig.clientListener.positions;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> lock(rig.clientListener.mutex);
                CHECK(rig.clientListener.shapes.size() == shapesBefore);
                CHECK(rig.clientListener.positions == positionsBefore);
            }

            screen.SetCursor(4, 0x00FF8800);
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.shapes.size() == shapesBefore + 1; }));
            {
                std::lock_guard<std::mutex> lock(rig.clientListener.mutex);
                const Marconi::CursorShape& shape = rig.clientListener.shapes.back();
                CHECK(shape.width == 4 && shape.height == 4 && shape.hotspotX == 1 && shape.hotspotY == 2);
                CHECK(shape.argb.size() == 16 && shape.argb[0] == 0xFFFF8800u);
            }

            screen.MovePointer(30, 20);
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.lastX == 30 && rig.clientListener.lastY == 20; }));

            // Hiding the pointer comes through as an empty image.
            screen.SetCursor(0, 0, true);
            CHECK(rig.clientListener.WaitFor([&]() { return rig.clientListener.shapes.size() == shapesBefore + 2; }));
            {
                std::lock_guard<std::mutex> lock(rig.clientListener.mutex);
                CHECK(rig.clientListener.shapes.back().argb.empty());
            }
        });
    }
} // namespace MarconiServerTest
