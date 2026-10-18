// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server over a real loopback socket: a client connects, handshakes and gets a frame through the Listener and
// the shared socket transport

#include "TestSupport.h"

#include <MarconiCore/Net/Socket.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiCore/Wire/WireChannel.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <thread>

namespace MarconiServerTest
{
    using namespace Marconi;

    void RunListenerTests()
    {
        RunTest("A viewer connects over a loopback socket and completes the handshake", []() {
            Server::TestPatternScreen screen(8, 8);
            RecordingInput input;
            Server::Settings settings;
            settings.port = 0; // a free port
            settings.serverName = "Socket PC";
            settings.serverVersion = "0.6.0.0";
            Server::Listener listener(screen, input, settings);
            listener.SetFrameIntervalMs(1);

            std::string error;
            CHECK(listener.Start(error, "127.0.0.1"));
            uint16_t port = listener.GetPort();
            CHECK(port != 0);

            std::thread serverThread([&]() { listener.ServeOne(5000); });

            Net::Socket clientSocket;
            CHECK(clientSocket.Connect("127.0.0.1", port, 5000, error));
            Wire::SocketTransport clientTransport(clientSocket);
            Wire::MessageReader reader(clientTransport);

            Wire::Hello hello;
            hello.clientName = "socket viewer";
            Wire::Bytes helloBytes = hello.Encode();
            CHECK(clientTransport.Write(helloBytes.data(), helloBytes.size()));

            Wire::Bytes welcomeBytes = reader.Next();
            Wire::Reader welcomeReader(welcomeBytes);
            CHECK(Wire::ReadType(welcomeReader) == Wire::Type::Welcome);
            Wire::Welcome welcome = Wire::Welcome::Decode(welcomeReader);
            CHECK(welcome.serverName == "Socket PC");
            CHECK(welcome.displays.size() == 1 && welcome.displays[0].width == 8);

            Wire::Bytes loginBytes = Wire::Login{""}.Encode();
            CHECK(clientTransport.Write(loginBytes.data(), loginBytes.size()));
            Wire::Bytes resultBytes = reader.Next();
            Wire::Reader resultReader(resultBytes);
            CHECK(Wire::ReadType(resultReader) == Wire::Type::LoginResult);
            CHECK(Wire::LoginResult::Decode(resultReader).status == Wire::LoginResult::Status::Ok);

            // At least one full frame follows.
            Wire::Bytes frameBytes = reader.Next();
            Wire::Reader frameReader(frameBytes);
            CHECK(Wire::ReadType(frameReader) == Wire::Type::FrameBegin);

            // Close the client; the server's session ends and ServeOne returns.
            clientSocket.Shutdown();
            clientSocket.Close();
            listener.Stop();
            serverThread.join();
            CHECK(true);
        });
    }
} // namespace MarconiServerTest
