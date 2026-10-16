// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the Extended Clipboard: message encoding, and the exchange with a fake server

#include "TestSupport.h"

#include <MarconiCore/Rfb/ExtendedClipboard.h>
#include <MarconiCore/Rfb/Messages.h>

#include <algorithm>

namespace MarconiTest
{
    using namespace Marconi;
    using namespace Marconi::Rfb::ExtendedClipboard;

    namespace
    {
        struct Session
        {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client{listener};
            std::vector<int32_t> encodings;

            explicit Session(bool extendedClipboard = true)
            {
                Rfb::ConnectionOptions options;
                options.host = "127.0.0.1";
                options.port = server.GetPort();
                options.extendedClipboard = extendedClipboard;
                client.Connect(options);
                if (!server.AcceptClient())
                {
                    throw std::runtime_error("client never connected");
                }
                server.HandshakeNone("RFB 003.008\n", 4, 3, "session");
                encodings = server.ReadClientSetup();
                if (!listener.WaitFor([&]() { return listener.connected; }))
                {
                    throw std::runtime_error("client never finished connecting");
                }
            }

            // The server's side of an extended message.
            void SendExtended(const Rfb::Bytes& body)
            {
                Rfb::OutBuffer out;
                out.U8(Rfb::ServerMessage::ServerCutText).Padding(3).S32(-static_cast<int32_t>(body.size()));
                out.Append(body.data(), body.size());
                server.Send(out);
            }

            // The next client message must be an extended clipboard message; returns its body.
            Rfb::Bytes ReadExtended()
            {
                Rfb::InStream& in = server.In();
                CHECK(in.U8() == Rfb::ClientMessage::ClientCutText);
                in.Skip(3);
                int32_t length = in.S32();
                CHECK(length < 0);
                return in.ReadBytes(static_cast<size_t>(-length));
            }

            // Announces the extension with these formats, and consumes the client's capabilities reply.
            Message Announce(const std::map<uint32_t, uint32_t>& sizes)
            {
                SendExtended(BuildCaps(sizes, Action::Request | Action::Peek | Action::Notify | Action::Provide));
                return Parse(ReadExtended(), 1024 * 1024);
            }
        };

        constexpr uint32_t kMega = 1024 * 1024;
    } // namespace

    void RunExtendedClipboardTests()
    {
        RunTest("Extended clipboard text is CRLF and NUL terminated on the wire", []() {
            Rfb::Bytes wire = TextToWire("a\nb\r\nc");
            CHECK(std::string(wire.begin(), wire.end()) == std::string("a\r\nb\r\nc\0", 8));
            CHECK(TextFromWire(wire) == "a\nb\nc");
            CHECK(TextFromWire(Rfb::Bytes{'x', '\r', 'y', 0, 'z'}) == "x\ry"); // a lone CR stays; NUL ends it
        });

        RunTest("Caps, notify and request messages have the documented layout", []() {
            Rfb::Bytes caps = BuildCaps({{Format::Text, 10}, {Format::Dib, 20}}, Action::Request | Action::Provide);
            CHECK(caps == HexBytes("13000009" "0000000a" "00000014"));
            CHECK(BuildNotify(Format::Text) == HexBytes("08000001"));
            CHECK(BuildRequest(Format::Text | Format::Dib) == HexBytes("02000009"));
            CHECK(BuildPeek() == HexBytes("04000000"));
            Rfb::Bytes wrapped = WrapClientMessage(BuildNotify(Format::Text));
            CHECK(wrapped == HexBytes("06000000" "fffffffc" "08000001"));

            Message parsed = Parse(caps, 1024);
            CHECK(parsed.Has(Action::Caps) && parsed.Has(Action::Request) && !parsed.Has(Action::Notify));
            CHECK(parsed.maxSizes.size() == 2 && parsed.maxSizes.at(Format::Text) == 10 &&
                  parsed.maxSizes.at(Format::Dib) == 20);
        });

        RunTest("Provide round-trips text and an image through zlib", []() {
            std::map<uint32_t, Rfb::Bytes> data = {{Format::Text, TextToWire("héllo")}, {Format::Dib, Rfb::Bytes(500, 7)}};
            Rfb::Bytes body = BuildProvide(data);
            CHECK(body.size() < 100); // the 500 identical bytes compress well
            Message parsed = Parse(body, 1024);
            CHECK(parsed.Has(Action::Provide));
            CHECK(parsed.GetFormats() == (Format::Text | Format::Dib));
            CHECK(TextFromWire(parsed.data.at(Format::Text)) == "héllo");
            CHECK(parsed.data.at(Format::Dib) == Rfb::Bytes(500, 7));
        });

        RunTest("Malformed extended clipboard messages are refused", []() {
            CHECK(ThrowsRfbError([]() { Parse(HexBytes("010000"), 1024); }, "truncated"));
            CHECK(ThrowsRfbError([]() { Parse(HexBytes("01000001"), 1024); }, "truncated")); // caps without a size
            Rfb::Bytes big = BuildProvide({{Format::Text, Rfb::Bytes(5000, 'a')}});
            CHECK(ThrowsRfbError([&]() { Parse(big, 1000); }, "oversized"));
            Rfb::Bytes corrupt = BuildProvide({{Format::Text, TextToWire("x")}});
            corrupt[5] ^= 0xff;
            CHECK(ThrowsRfbError([&]() { Parse(corrupt, 1024); }, "corrupt"));
        });

        RunTest("The client advertises the extension and answers the server's capabilities", []() {
            Session s;
            CHECK(std::find(s.encodings.begin(), s.encodings.end(), kPseudoEncoding) != s.encodings.end());
            CHECK(!s.client.HasExtendedClipboard());
            Message reply = s.Announce({{Format::Text, kMega}, {Format::Dib, kMega}});
            CHECK(reply.Has(Action::Caps) && reply.Has(Action::Request) && reply.Has(Action::Provide));
            CHECK(reply.maxSizes.at(Format::Text) == Rfb::ClipboardChannel::kMaxTextBytes);
            CHECK(reply.maxSizes.at(Format::Dib) == Rfb::ClipboardChannel::kMaxDibBytes);
            CHECK(s.client.HasExtendedClipboard());
            CHECK(s.client.ServerAcceptsClipboardImages());
        });

        RunTest("The extension is not advertised when switched off", []() {
            Session s(false);
            CHECK(std::find(s.encodings.begin(), s.encodings.end(), kPseudoEncoding) == s.encodings.end());
            s.SendExtended(BuildCaps({{Format::Text, kMega}}, Action::Request));
            Rfb::OutBuffer bell;
            bell.U8(Rfb::ServerMessage::Bell);
            s.server.Send(bell);
            CHECK(s.listener.WaitFor([&]() { return s.listener.bells == 1; })); // the message was skipped cleanly
            CHECK(!s.client.HasExtendedClipboard());
        });

        RunTest("A server that takes no text leaves the extension unused", []() {
            Session s;
            s.Announce({{Format::Text, 0}, {Format::Dib, kMega}});
            CHECK(!s.client.HasExtendedClipboard());
            Rfb::ClipboardContent content;
            content.utf8Text = "hello";
            CHECK(!s.client.OfferClipboard(content));
        });

        RunTest("The server's clipboard arrives as UTF-8 after a notify, request, provide exchange", []() {
            Session s;
            s.Announce({{Format::Text, kMega}});
            s.SendExtended(BuildNotify(Format::Text | Format::Html));
            Message request = Parse(s.ReadExtended(), 1024);
            CHECK(request.Has(Action::Request) && request.GetFormats() == Format::Text); // only what we take
            s.SendExtended(BuildProvide({{Format::Text, TextToWire("héllo\nwörld")}}));
            CHECK(s.listener.WaitFor([&]() { return s.listener.clipboards.size() == 1; }));
            CHECK(s.listener.clipboards[0].utf8Text == "héllo\nwörld");
            CHECK(s.listener.clipboards[0].dib.empty());
            CHECK(s.listener.cutTexts.empty());
        });

        RunTest("Requesting the server's clipboard asks for every format it offers", []() {
            Session s;
            s.Announce({{Format::Text, kMega}, {Format::Dib, kMega}});
            CHECK(s.client.RequestServerClipboard());
            Message request = Parse(s.ReadExtended(), 1024);
            CHECK(request.Has(Action::Request) && request.GetFormats() == (Format::Text | Format::Dib));
            s.SendExtended(BuildProvide({{Format::Text, TextToWire("t")}, {Format::Dib, Rfb::Bytes{1, 2, 3}}}));
            CHECK(s.listener.WaitFor([&]() { return s.listener.clipboards.size() == 1; }));
            CHECK(s.listener.clipboards[0].utf8Text == "t" && s.listener.clipboards[0].dib == (Rfb::Bytes{1, 2, 3}));
        });

        RunTest("Our clipboard is offered, then provided when the server asks", []() {
            Session s;
            s.Announce({{Format::Text, kMega}});
            Rfb::ClipboardContent content;
            content.utf8Text = "ünï\ncode";
            content.dib = Rfb::Bytes(10, 1); // the server doesn't take images: not offered
            CHECK(s.client.OfferClipboard(content));
            Message notify = Parse(s.ReadExtended(), 1024);
            CHECK(notify.Has(Action::Notify) && notify.GetFormats() == Format::Text);

            s.SendExtended(BuildPeek());
            Message again = Parse(s.ReadExtended(), 1024);
            CHECK(again.Has(Action::Notify) && again.GetFormats() == Format::Text);

            s.SendExtended(BuildRequest(Format::Text | Format::Dib));
            Message provide = Parse(s.ReadExtended(), kMega);
            CHECK(provide.Has(Action::Provide) && provide.GetFormats() == Format::Text);
            const Rfb::Bytes& wire = provide.data.at(Format::Text);
            CHECK(std::string(wire.begin(), wire.end()) == std::string("ünï\r\ncode\0", 12));
            CHECK(TextFromWire(wire) == "ünï\ncode");
        });

        RunTest("An image is offered and provided when the server takes images", []() {
            Session s;
            s.Announce({{Format::Text, kMega}, {Format::Dib, kMega}});
            Rfb::ClipboardContent content;
            content.dib = Rfb::Bytes(300, 9);
            CHECK(s.client.OfferClipboard(content));
            Message notify = Parse(s.ReadExtended(), 1024);
            CHECK(notify.GetFormats() == Format::Dib);
            s.SendExtended(BuildRequest(Format::Dib));
            Message provide = Parse(s.ReadExtended(), kMega);
            CHECK(provide.data.at(Format::Dib) == Rfb::Bytes(300, 9));
        });

        RunTest("Extended messages before any capabilities are ignored, and Latin-1 text still works", []() {
            Session s;
            s.SendExtended(BuildNotify(Format::Text));
            s.SendExtended(BuildProvide({{Format::Text, TextToWire("ignored")}}));
            Rfb::OutBuffer legacy;
            legacy.U8(Rfb::ServerMessage::ServerCutText).Padding(3).U32(5).Append("plain");
            s.server.Send(legacy);
            CHECK(s.listener.WaitFor([&]() { return s.listener.cutTexts.size() == 1; }));
            CHECK(s.listener.cutTexts[0] == "plain");
            CHECK(s.listener.clipboards.empty());
            CHECK(s.client.IsConnected());
        });

        RunTest("An oversized extended clipboard message disconnects with an error", []() {
            Session s;
            Rfb::OutBuffer out;
            out.U8(Rfb::ServerMessage::ServerCutText).Padding(3).S32(-(16 * 1024 * 1024 + 1));
            s.server.Send(out);
            CHECK(s.listener.WaitFor([&]() { return s.listener.disconnected; }));
            CHECK(s.listener.disconnectError.find("oversized clipboard update") != std::string::npos);
        });
    }
} // namespace MarconiTest
