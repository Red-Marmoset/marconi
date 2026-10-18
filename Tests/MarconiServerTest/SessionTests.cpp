// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Drives a Session from an in-memory viewer: the handshake, the first full frame, input reaching the sink, and a
// clean disconnect

#include "TestSupport.h"

#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        // The viewer's side of the connection: send framed messages, and read whole messages one at a time.
        class Viewer
        {
        public:
            explicit Viewer(Wire::Transport& transport) : m_reader(transport), m_transport(transport) {}

            void Send(const Wire::Bytes& framed) { m_transport.Write(framed.data(), framed.size()); }

            // Reads the next message and returns its type, leaving `body` positioned after the type byte.
            Wire::Type Next(Wire::Bytes& bytesOut, Wire::Reader& readerOut)
            {
                bytesOut = m_reader.Next();
                readerOut = Wire::Reader(bytesOut);
                return Wire::ReadType(readerOut);
            }

        private:
            Wire::MessageReader m_reader;
            Wire::Transport& m_transport;
        };
    } // namespace

    void RunSessionTests()
    {
        RunTest("A viewer connects, gets a full frame, and its input reaches the computer", []() {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);

            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            Server::Settings settings;
            settings.serverName = "Test PC";
            settings.serverVersion = "0.6.0.0";
            Server::Session session(serverTransport, screen, input, settings);
            session.SetFrameIntervalMs(1);

            std::thread serverThread([&]() { session.Run(); });

            Viewer viewer(clientTransport);
            Wire::Bytes bytes;
            Wire::Reader body(nullptr, 0);

            // Hello -> Welcome.
            Wire::Hello hello;
            hello.clientName = "in-memory viewer";
            hello.capabilities = Wire::Capability::MultipleDisplays | Wire::Capability::Clipboard;
            viewer.Send(hello.Encode());
            CHECK(viewer.Next(bytes, body) == Wire::Type::Welcome);
            Wire::Welcome welcome = Wire::Welcome::Decode(body);
            CHECK(welcome.serverName == "Test PC");
            CHECK(welcome.serverVersion == "0.6.0.0");
            CHECK(welcome.displays.size() == 1 && welcome.displays[0].width == 4 && welcome.displays[0].height == 3);
            // The server grants only the capabilities it shares with the viewer: it does displays, not clipboard yet.
            CHECK(welcome.capabilities == Wire::Capability::MultipleDisplays);

            // Login -> LoginResult (the skeleton has no password, so it lets the viewer in).
            viewer.Send(Wire::Login{""}.Encode());
            CHECK(viewer.Next(bytes, body) == Wire::Type::LoginResult);
            CHECK(Wire::LoginResult::Decode(body).status == Wire::LoginResult::Status::Ok);

            // The first picture: FrameBegin, one Still covering the whole 4x3 desktop, FrameEnd.
            CHECK(viewer.Next(bytes, body) == Wire::Type::FrameBegin);
            uint64_t frameId = Wire::FrameBegin::Decode(body).frameId;
            CHECK(viewer.Next(bytes, body) == Wire::Type::Still);
            Wire::Still still = Wire::Still::Decode(body);
            CHECK(still.rect.x == 0 && still.rect.y == 0 && still.rect.width == 4 && still.rect.height == 3);
            CHECK(still.compression == Wire::Still::Compression::None);
            CHECK(still.data.size() == 4u * 3u * 4u); // four bytes a pixel, little-endian B,G,R,0
            // On frame 0 the bright bar sits in column 0, so the top-left pixel is white.
            CHECK(still.data[0] == 0xFF && still.data[1] == 0xFF && still.data[2] == 0xFF && still.data[3] == 0x00);
            // The top-right pixel (column 3) is off the bar: the gradient's red corner (red 255, green 0, blue 0).
            CHECK(still.data[12] == 0x00 && still.data[13] == 0x00 && still.data[14] == 0xFF && still.data[15] == 0x00);
            CHECK(viewer.Next(bytes, body) == Wire::Type::FrameEnd);
            CHECK(Wire::FrameEnd::Decode(body).frameId == frameId);

            // Input the viewer sends reaches the sink. Keep draining frames while waiting for the Pong, so the
            // server's frame loop never blocks on a full pipe.
            Wire::Pointer pointer;
            pointer.x = 2;
            pointer.y = 1;
            pointer.buttons = 0x01;
            viewer.Send(pointer.Encode());
            Wire::Key key;
            key.keysym = 0x0062; // 'b'
            key.scancode = 0x05;
            key.down = true;
            viewer.Send(key.Encode());
            Wire::Ping ping;
            ping.token = 0xABCD1234;
            viewer.Send(ping.Encode());

            bool gotPong = false;
            for (int i = 0; i < 100000 && !gotPong; ++i)
            {
                Wire::Type type = viewer.Next(bytes, body);
                if (type == Wire::Type::Pong)
                {
                    CHECK(Wire::Pong::Decode(body).token == 0xABCD1234);
                    gotPong = true;
                }
            }
            CHECK(gotPong);

            {
                std::lock_guard<std::mutex> lock(input.mutex);
                CHECK(input.pointers.size() == 1);
                CHECK(input.pointers[0].x == 2 && input.pointers[0].y == 1 && input.pointers[0].buttons == 0x01);
                CHECK(input.keys.size() == 1);
                CHECK(input.keys[0].keysym == 0x0062 && input.keys[0].scancode == 0x05 && input.keys[0].down);
            }

            // Bye ends the session; closing both pipes unblocks any thread still waiting.
            viewer.Send(Wire::Bye{"done"}.Encode());
            session.Stop();
            clientToServer.Close();
            serverToClient.Close();
            serverThread.join();
            CHECK(screen.FrameCount() >= 1);
        });

        RunTest("A client that doesn't start with Hello is dropped", []() {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);

            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            Server::Session session(serverTransport, screen, input, Server::Settings{});
            std::thread serverThread([&]() { session.Run(); });

            // Send a Pointer instead of Hello: the server drops the connection without ever sending a Welcome.
            Wire::Bytes wrong = Wire::Pointer{}.Encode();
            clientTransport.Write(wrong.data(), wrong.size());

            // The viewer's read returns end of stream once the server has closed, not a Welcome.
            serverThread.join(); // Run() returns on its own because the handshake was refused
            serverToClient.Close();
            uint8_t buffer[16];
            CHECK(serverToClient.Read(buffer, sizeof(buffer)) == 0);
            CHECK(input.pointers.empty()); // input before a successful handshake is never applied
        });
    }
} // namespace MarconiServerTest
