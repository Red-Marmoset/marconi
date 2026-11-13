// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clipboard sharing, end to end: the real Wire client against the real server over a loopback socket, with the
// computer's clipboard replaced by an in-memory one. Text and images both ways, the size limit, repeated copies, and
// that a clipboard the server just set isn't sent back (a loop).

#include "TestSupport.h"

#include <MarconiCore/Wire/WireClient.h>
#include <MarconiCore/Wire/WireImage.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/Session.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <memory>
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

        void Settle(int ms = 400)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }

        // A 24-bit bottom-up DIB, laid out as PngToDib writes one, with a pattern that differs per pixel.
        Wire::Bytes MakeDib(int width, int height, uint8_t seed)
        {
            Wire::Bytes dib;
            auto put32 = [&dib](uint32_t v) {
                for (int i = 0; i < 4; ++i)
                {
                    dib.push_back(static_cast<uint8_t>(v >> (8 * i)));
                }
            };
            size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
            put32(40);
            put32(static_cast<uint32_t>(width));
            put32(static_cast<uint32_t>(height));
            dib.push_back(1);
            dib.push_back(0);
            dib.push_back(24);
            dib.push_back(0);
            put32(0);
            put32(static_cast<uint32_t>(stride * height));
            put32(2835);
            put32(2835);
            put32(0);
            put32(0);
            for (int y = 0; y < height; ++y)
            {
                size_t start = dib.size();
                dib.resize(start + stride, 0);
                for (int x = 0; x < width; ++x)
                {
                    dib[start + x * 3 + 0] = static_cast<uint8_t>(seed + x * 7 + y);
                    dib[start + x * 3 + 1] = static_cast<uint8_t>(seed + y * 13);
                    dib[start + x * 3 + 2] = static_cast<uint8_t>(x * 31 + y * 3);
                }
            }
            return dib;
        }

        Server::ClipboardContents TextContents(const std::string& text)
        {
            Server::ClipboardContents contents;
            contents.text = text;
            return contents;
        }

        // The server and a connected viewer, with the in-memory clipboard shared (or not).
        struct Rig
        {
            Server::TestPatternScreen screen{8, 8};
            RecordingInput input;
            MemoryClipboard clipboard;
            Server::Settings settings;
            std::unique_ptr<Server::Listener> listener;
            std::thread serverThread;
            RecordingClientListener clientListener;
            std::unique_ptr<Wire::Client> client;

            explicit Rig(bool shareClipboard = true)
            {
                settings.port = 0;
                settings.serverName = "Clipboard PC";
                settings.serverVersion = "0.9.9.0";
                listener = std::make_unique<Server::Listener>(screen, input, settings);
                listener->SetFrameIntervalMs(1);
                if (shareClipboard)
                {
                    listener->SetClipboard(&clipboard);
                }
                std::string error;
                CHECK(listener->Start(error, "127.0.0.1"));
                serverThread = std::thread([this]() { listener->ServeOne(5000); });
                client = std::make_unique<Wire::Client>(clientListener);
                Wire::ClientOptions options;
                options.host = "127.0.0.1";
                options.port = listener->GetPort();
                options.clientName = "clipboard viewer";
                client->Connect(options);
                CHECK(clientListener.WaitFor([this]() { return clientListener.connected; }));
            }

            ~Rig()
            {
                client->Disconnect();
                clientListener.WaitFor([this]() { return clientListener.disconnected; });
                listener->Stop();
                serverThread.join();
            }

            size_t ClipboardsDelivered()
            {
                std::lock_guard<std::mutex> lock(clientListener.mutex);
                return clientListener.clipboards.size();
            }

            Rfb::ClipboardContent LastDelivered()
            {
                std::lock_guard<std::mutex> lock(clientListener.mutex);
                return clientListener.clipboards.empty() ? Rfb::ClipboardContent() : clientListener.clipboards.back();
            }

            Server::ClipboardContents ServerHas()
            {
                std::lock_guard<std::mutex> lock(clipboard.mutex);
                return clipboard.contents;
            }

            int ServerWrites()
            {
                std::lock_guard<std::mutex> lock(clipboard.mutex);
                return clipboard.writes;
            }
        };
    } // namespace

    void RunClipboardTests()
    {
        RunTest("A DIB becomes a PNG and back without changing a pixel; bad data is refused", []() {
            Wire::Bytes dib = MakeDib(5, 3, 17); // 5 pixels wide: rows are padded to a multiple of four bytes
            Wire::Bytes png;
            CHECK(Wire::DibToPng(dib, png));
            CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
            Wire::Bytes back;
            CHECK(Wire::PngToDib(png, back));
            CHECK(back == dib);

            Wire::Bytes garbage = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            Wire::Bytes out;
            CHECK(!Wire::PngToDib(garbage, out));
            CHECK(!Wire::DibToPng(garbage, out));
            png.resize(png.size() / 2); // cut short
            CHECK(!Wire::PngToDib(png, out));
        });

        RunTest("Clipboard text goes from the viewer to the server, and is not echoed back", []() {
            Rig rig;
            CHECK(rig.client->HasExtendedClipboard());
            Rfb::ClipboardContent content;
            content.utf8Text = "Hello from the viewer \xC3\xA9\n\xE2\x82\xAC";
            CHECK(rig.client->OfferClipboard(content));
            CHECK(WaitUntil([&]() { return rig.ServerHas().text == content.utf8Text; }, 5000));
            Settle();
            CHECK(rig.ServerWrites() == 1);       // set once
            CHECK(rig.ClipboardsDelivered() == 0); // and never offered back, though the write reported a change
        });

        RunTest("Clipboard text goes from the server to the viewer, and the viewer's get asks for it", []() {
            Rig rig;
            rig.clipboard.SimulateCopy(TextContents("Copied on the server"));
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 1; }, 5000));
            CHECK(rig.LastDelivered().utf8Text == "Copied on the server");
            CHECK(rig.LastDelivered().dib.empty());

            // Something already on the server's clipboard when the viewer asks (the Get menu action).
            rig.clipboard.Preset(TextContents("Already there"));
            CHECK(rig.client->RequestServerClipboard());
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 2; }, 5000));
            CHECK(rig.LastDelivered().utf8Text == "Already there");
            Settle();
            CHECK(rig.ClipboardsDelivered() == 2);
            CHECK(rig.ServerWrites() == 0);
        });

        RunTest("A clipboard image goes both ways as PNG on the wire", []() {
            Rig rig;
            CHECK(rig.client->ServerAcceptsClipboardImages());
            Wire::Bytes dib = MakeDib(20, 10, 3);

            Rfb::ClipboardContent content;
            content.dib = dib;
            CHECK(rig.client->OfferClipboard(content));
            CHECK(WaitUntil([&]() { return !rig.ServerHas().png.empty(); }, 5000));
            Wire::Bytes png = rig.ServerHas().png;
            CHECK(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
            Wire::Bytes decoded;
            CHECK(Wire::PngToDib(png, decoded) && decoded == dib);
            Settle();
            CHECK(rig.ServerWrites() == 1);
            CHECK(rig.ClipboardsDelivered() == 0);

            // The other way: an image copied on the server arrives as a DIB.
            Wire::Bytes dib2 = MakeDib(9, 9, 99);
            Server::ClipboardContents copied;
            CHECK(Wire::DibToPng(dib2, copied.png));
            rig.clipboard.SimulateCopy(copied);
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 1; }, 5000));
            CHECK(rig.LastDelivered().dib == dib2);
            CHECK(rig.LastDelivered().utf8Text.empty());
        });

        RunTest("Text and an image together arrive as one clipboard", []() {
            Rig rig;
            Server::ClipboardContents copied;
            copied.text = "caption";
            Wire::Bytes dib = MakeDib(4, 4, 1);
            CHECK(Wire::DibToPng(dib, copied.png));
            rig.clipboard.SimulateCopy(copied);
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() >= 1; }, 5000));
            Settle();
            CHECK(rig.ClipboardsDelivered() == 1);
            CHECK(rig.LastDelivered().utf8Text == "caption" && rig.LastDelivered().dib == dib);
        });

        RunTest("Clipboard content over the size limit is not sent in either direction", []() {
            Rig rig;
            // Too big from the viewer: the offer is refused up front, the server hears nothing.
            Rfb::ClipboardContent big;
            big.utf8Text.assign(Wire::kMaxClipboardTextBytes + 1, 'x');
            CHECK(!rig.client->OfferClipboard(big));
            Settle(200);
            CHECK(rig.ServerWrites() == 0 && rig.ServerHas().IsEmpty());

            // Too big from the server: not offered, but a small image copied with it still is.
            Server::ClipboardContents copied;
            copied.text.assign(Wire::kMaxClipboardTextBytes + 1, 'y');
            Wire::Bytes dib = MakeDib(3, 3, 5);
            CHECK(Wire::DibToPng(dib, copied.png));
            rig.clipboard.SimulateCopy(copied);
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() >= 1; }, 5000));
            Settle();
            CHECK(rig.ClipboardsDelivered() == 1);
            CHECK(rig.LastDelivered().utf8Text.empty() && rig.LastDelivered().dib == dib);

            // Only oversize text: nothing at all is offered.
            rig.clipboard.SimulateCopy(TextContents(std::string(Wire::kMaxClipboardTextBytes + 1, 'z')));
            Settle();
            CHECK(rig.ClipboardsDelivered() == 1);

            // Just at the limit still travels.
            std::string exact(Wire::kMaxClipboardTextBytes, 'w');
            Rfb::ClipboardContent atLimit;
            atLimit.utf8Text = exact;
            CHECK(rig.client->OfferClipboard(atLimit));
            CHECK(WaitUntil([&]() { return rig.ServerHas().text == exact; }, 10000));
        });

        RunTest("A viewer that ignores the size limit, or sends data nobody asked for, changes nothing", []() {
            Pipe clientToServer;
            Pipe serverToClient;
            PipeTransport serverTransport(clientToServer, serverToClient);
            PipeTransport clientTransport(serverToClient, clientToServer);
            Server::TestPatternScreen screen(4, 3);
            RecordingInput input;
            MemoryClipboard clipboard;
            Server::Settings settings;
            Server::Session session(serverTransport, screen, input, settings);
            session.SetClipboard(&clipboard);
            session.SetFrameIntervalMs(1);
            std::thread serverThread([&]() { session.Run(); });

            Wire::MessageReader reader(clientTransport);
            auto send = [&](const Wire::Bytes& framed) { clientTransport.Write(framed.data(), framed.size()); };
            auto next = [&](Wire::Bytes& bytes) {
                bytes = reader.Next();
                Wire::Reader body(bytes);
                return Wire::ReadType(body);
            };
            Wire::Bytes bytes;
            Wire::Hello hello;
            hello.capabilities = Wire::Capability::Clipboard;
            send(hello.Encode());
            CHECK(next(bytes) == Wire::Type::Welcome);
            {
                Wire::Reader body(bytes);
                Wire::ReadType(body);
                CHECK((Wire::Welcome::Decode(body).capabilities & Wire::Capability::Clipboard) != 0);
            }
            send(Wire::Login{"x"}.Encode());
            CHECK(next(bytes) == Wire::Type::LoginResult);

            // Data nobody asked for.
            Wire::ClipboardData unasked;
            unasked.format = Wire::ClipboardRequest::Format::Text;
            unasked.data = {'h', 'i'};
            send(unasked.Encode());

            // An offer, answered with more than the limit.
            Wire::ClipboardOffer offer;
            offer.hasText = true;
            send(offer.Encode());
            Wire::Type type;
            do
            {
                type = next(bytes); // skip picture updates until the server's request for the text arrives
            } while (type != Wire::Type::ClipboardRequest);
            Wire::ClipboardData oversize;
            oversize.format = Wire::ClipboardRequest::Format::Text;
            oversize.data.assign(Wire::kMaxClipboardTextBytes + 1, 'x');
            send(oversize.Encode());
            Settle();
            {
                std::lock_guard<std::mutex> lock(clipboard.mutex);
                CHECK(clipboard.writes == 0 && clipboard.contents.IsEmpty());
            }

            // The connection is still good: a proper offer after that works.
            send(offer.Encode());
            do
            {
                type = next(bytes);
            } while (type != Wire::Type::ClipboardRequest);
            Wire::ClipboardData proper;
            proper.format = Wire::ClipboardRequest::Format::Text;
            proper.data = {'o', 'k'};
            send(proper.Encode());
            CHECK(WaitUntil(
                [&]() {
                    std::lock_guard<std::mutex> lock(clipboard.mutex);
                    return clipboard.contents.text == "ok";
                },
                5000));

            send(Wire::Bye{}.Encode());
            serverThread.join();
        });

        RunTest("Many quick copies in both directions end on the last one and never loop", []() {
            Rig rig;
            for (int i = 0; i < 25; ++i)
            {
                Rfb::ClipboardContent content;
                content.utf8Text = "viewer " + std::to_string(i);
                CHECK(rig.client->OfferClipboard(content));
            }
            CHECK(WaitUntil([&]() { return rig.ServerHas().text == "viewer 24"; }, 5000));
            Settle(500);
            CHECK(rig.ServerHas().text == "viewer 24");
            CHECK(rig.ServerWrites() <= 25);
            const int writes = rig.ServerWrites();
            CHECK(rig.ClipboardsDelivered() == 0); // none of the viewer's own copies came back

            for (int i = 0; i < 25; ++i)
            {
                rig.clipboard.SimulateCopy(TextContents("server " + std::to_string(i)));
            }
            CHECK(WaitUntil([&]() { return rig.LastDelivered().utf8Text == "server 24"; }, 5000));
            Settle(500);
            const size_t delivered = rig.ClipboardsDelivered();
            CHECK(delivered >= 1 && delivered <= 25);
            CHECK(rig.LastDelivered().utf8Text == "server 24");
            CHECK(rig.ServerWrites() == writes); // the viewer didn't write anything back
            Settle(300);
            CHECK(rig.ClipboardsDelivered() == delivered); // and it stays quiet
        });

        RunTest("The same text copied again on the server is not offered twice", []() {
            Rig rig;
            rig.clipboard.SimulateCopy(TextContents("same"));
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 1; }, 5000));
            rig.clipboard.SimulateCopy(TextContents("same"));
            Settle();
            CHECK(rig.ClipboardsDelivered() == 1);
            rig.clipboard.SimulateCopy(TextContents("different"));
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 2; }, 5000));
            rig.clipboard.SimulateCopy(TextContents("same")); // a change back to something older still counts
            CHECK(WaitUntil([&]() { return rig.ClipboardsDelivered() == 3; }, 5000));
        });

        RunTest("A server without clipboard sharing doesn't grant it, and the viewer says so", []() {
            Rig rig(false);
            CHECK(!rig.client->HasExtendedClipboard());
            CHECK(!rig.client->ServerAcceptsClipboardImages());
            Rfb::ClipboardContent content;
            content.utf8Text = "nowhere to go";
            CHECK(!rig.client->OfferClipboard(content));
            CHECK(!rig.client->RequestServerClipboard());
        });
    }
} // namespace MarconiServerTest
