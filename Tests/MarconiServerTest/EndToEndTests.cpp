// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The real client against the real server over a loopback socket: the Wire client connects to the Listener, gets
// the test pattern into its framebuffer, and its input reaches the server's input sink

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs)
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
    } // namespace

    void RunEndToEndTests()
    {
        RunTest("The Wire client connects to the server, shows the screen, and sends input back", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.port = 0;
            settings.serverName = "End To End PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);

            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            std::thread serverThread([&]() { listener.ServeOne(5000); });

            RecordingClientListener clientListener;
            Wire::Client client(clientListener);
            Wire::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = listener.GetPort();
            options.clientName = "end-to-end viewer";
            client.Connect(options);

            CHECK(clientListener.WaitFor([&]() { return clientListener.connected; }));
            CHECK(clientListener.serverInit.name == "End To End PC");
            CHECK(clientListener.serverInit.width == 8 && clientListener.serverInit.height == 8);
            CHECK(client.IsConnected());

            // Wait for at least one picture update, then check the framebuffer holds the test pattern. The top-right
            // pixel (column 7, row 0) is off the moving bar: the gradient's red corner, whatever frame it is.
            CHECK(clientListener.WaitFor([&]() { return clientListener.updates >= 1; }));
            {
                Rfb::Framebuffer& fb = client.GetFramebuffer();
                std::lock_guard<std::mutex> lock(fb.GetMutex());
                CHECK(fb.GetWidth() == 8 && fb.GetHeight() == 8);
                uint32_t corner = fb.GetPixel(7, 0);
                CHECK(((corner >> 16) & 0xFF) == 0xFF); // red full
                CHECK(((corner >> 8) & 0xFF) == 0x00);  // green none at the top row
            }

            // Input the viewer sends reaches the server's sink.
            client.SendPointerEvent(3, 4, 0x01);
            client.SendKeyEvent(0x0062 /* 'b' */, true);
            CHECK(WaitUntil(
                [&]() {
                    std::lock_guard<std::mutex> lock(input.mutex);
                    return !input.pointers.empty() && !input.keys.empty();
                },
                5000));
            {
                std::lock_guard<std::mutex> lock(input.mutex);
                CHECK(input.pointers.front().x == 3 && input.pointers.front().y == 4 &&
                      input.pointers.front().buttons == 0x01);
                CHECK(input.keys.front().keysym == 0x0062 && input.keys.front().down);
            }

            client.Disconnect();
            CHECK(clientListener.WaitFor([&]() { return clientListener.disconnected; }));
            listener.Stop();
            serverThread.join();
        });
    }
} // namespace MarconiServerTest
