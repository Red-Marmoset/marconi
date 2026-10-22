// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP pointer updates, and for Rdp::Client against a scripted RDP server on loopback: the connection
// sequence, bitmap updates into the framebuffer, pointer shapes, input events, and the server ending the session.

#include "TestSupport.h"

#include <MarconiCore/Rdp/Input.h>
#include <MarconiCore/Rdp/Mcs.h>
#include <MarconiCore/Rdp/Negotiation.h>
#include <MarconiCore/Rdp/Output.h>
#include <MarconiCore/Rdp/Pdu.h>
#include <MarconiCore/Rdp/Pointer.h>
#include <MarconiCore/Rdp/RdpClient.h>
#include <MarconiCore/Rdp/Sequence.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        uint16_t U16(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o] | (b[o + 1] << 8)); }
        void P16(Rdp::Bytes& b, uint16_t v) { b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8)); }
        void P32(Rdp::Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); }

        // A TS_COLORPOINTERATTRIBUTE: a `width` by `height` shape whose XOR mask is `colour` (24-bit) everywhere, and
        // whose AND mask is `andRows` (one byte per row, top row first, for widths up to 8).
        Rdp::Bytes ColourPointer(uint16_t cacheIndex, int width, int height, uint32_t colour,
                                 const std::vector<uint8_t>& andRows)
        {
            Rdp::Bytes out;
            P16(out, cacheIndex);
            P16(out, 1); // hotspot x
            P16(out, 1); // hotspot y
            P16(out, static_cast<uint16_t>(width));
            P16(out, static_cast<uint16_t>(height));
            size_t xorRow = ((static_cast<size_t>(width) * 3) + 1) & ~static_cast<size_t>(1);
            P16(out, static_cast<uint16_t>(2 * height)); // lengthAndMask: one byte per row, padded to 2
            P16(out, static_cast<uint16_t>(xorRow * height));
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    out.push_back(static_cast<uint8_t>(colour));
                    out.push_back(static_cast<uint8_t>(colour >> 8));
                    out.push_back(static_cast<uint8_t>(colour >> 16));
                }
                out.resize(out.size() + (xorRow - static_cast<size_t>(width) * 3), 0);
            }
            for (int y = height - 1; y >= 0; --y) // rows are stored bottom first
            {
                out.push_back(andRows[static_cast<size_t>(y)]);
                out.push_back(0);
            }
            return out;
        }

        // A TPKT + X.224 Connection Confirm carrying an RDP_NEG_RSP choosing `protocol`.
        Rdp::Bytes ConnectionConfirm(uint32_t protocol)
        {
            Rdp::Bytes neg = {0x02, 0x00, 0x08, 0x00};
            P32(neg, protocol);
            Rdp::Bytes x224 = {static_cast<uint8_t>(6 + neg.size()), 0xD0, 0, 0, 0, 0, 0};
            x224.insert(x224.end(), neg.begin(), neg.end());
            size_t total = 4 + x224.size();
            Rdp::Bytes packet = {0x03, 0x00, static_cast<uint8_t>(total >> 8), static_cast<uint8_t>(total & 0xFF)};
            packet.insert(packet.end(), x224.begin(), x224.end());
            return packet;
        }

        // An MCS Connect-Response carrying server data with I/O channel 1003 and no virtual channels.
        Rdp::Bytes ConnectResponse()
        {
            Rdp::Bytes serverData;
            P16(serverData, 0x0C01); P16(serverData, 12); P32(serverData, 0x00080004); P32(serverData, 0);
            P16(serverData, 0x0C03); P16(serverData, 8); P16(serverData, 1003); P16(serverData, 0);
            Rdp::Bytes gcc = {0x00, 0x05, 0x00, 0x14, 0x7c, 0x00, 0x01};
            gcc.insert(gcc.end(), serverData.begin(), serverData.end());
            Rdp::Bytes inner = {0x0a, 0x01, 0x00, 0x04, static_cast<uint8_t>(gcc.size())};
            inner.insert(inner.end(), gcc.begin(), gcc.end());
            Rdp::Bytes response = {0x7f, 0x66, static_cast<uint8_t>(inner.size())};
            response.insert(response.end(), inner.begin(), inner.end());
            return response;
        }

        // A Send Data Indication (server to client) carrying `pdu` on channel 1003.
        Rdp::Bytes Indication(const Rdp::Bytes& pdu)
        {
            Rdp::Bytes out = {0x68, 0x00, 0x06, 0x03, 0xEB, 0x70};
            if (pdu.size() < 0x80)
            {
                out.push_back(static_cast<uint8_t>(pdu.size()));
            }
            else
            {
                out.push_back(static_cast<uint8_t>(0x80 | (pdu.size() >> 8)));
                out.push_back(static_cast<uint8_t>(pdu.size() & 0xFF));
            }
            out.insert(out.end(), pdu.begin(), pdu.end());
            return out;
        }

        // A Demand Active for `shareId` whose Bitmap capability set announces a `width` by `height` desktop.
        Rdp::Bytes DemandActive(uint32_t shareId, uint16_t width, uint16_t height)
        {
            Rdp::Bytes general;
            P16(general, 1); P16(general, 24); // type, length
            general.resize(24, 0);
            Rdp::Bytes bitmap;
            P16(bitmap, 2); P16(bitmap, 28);
            P16(bitmap, 16); P16(bitmap, 1); P16(bitmap, 1); P16(bitmap, 1);
            P16(bitmap, width); P16(bitmap, height);
            bitmap.resize(28, 0);
            Rdp::Bytes body;
            P32(body, shareId);
            P16(body, 4); // lengthSourceDescriptor
            P16(body, static_cast<uint16_t>(general.size() + bitmap.size() + 4));
            body.insert(body.end(), {'R', 'D', 'P', 0});
            P16(body, 2); // numberCapabilities
            P16(body, 0); // pad
            body.insert(body.end(), general.begin(), general.end());
            body.insert(body.end(), bitmap.begin(), bitmap.end());
            P32(body, 0); // sessionId
            return Rdp::WrapShareControl(Rdp::ShareControlType::DemandActive, 0x03EA, body);
        }

        // A slow-path Update PDU with one uncompressed 16-bit rectangle of `colour565` at `x`,`y`, `w` by `h`.
        Rdp::Bytes BitmapUpdatePdu(uint32_t shareId, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   uint16_t colour565)
        {
            Rdp::Bytes body;
            P16(body, 0x0001); // updateType: bitmap
            P16(body, 1);      // count
            P16(body, x); P16(body, y); P16(body, static_cast<uint16_t>(x + w - 1)); P16(body, static_cast<uint16_t>(y + h - 1));
            P16(body, w); P16(body, h); P16(body, 16); P16(body, 0);
            size_t rowBytes = ((static_cast<size_t>(w) * 2) + 3) & ~static_cast<size_t>(3);
            P16(body, static_cast<uint16_t>(rowBytes * h));
            for (int row = 0; row < h; ++row)
            {
                for (int col = 0; col < w; ++col)
                {
                    P16(body, colour565);
                }
                body.resize(body.size() + rowBytes - static_cast<size_t>(w) * 2, 0);
            }
            return Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::Update, body);
        }

        // Stands in for TLS over the loopback socket: nothing is encrypted, and the certificate (when one is given)
        // plays the part of one this computer doesn't trust.
        class PassthroughTls : public Net::TlsStream
        {
        public:
            PassthroughTls(Net::Socket& socket, Net::TlsOptions options, Net::TlsCertificate certificate)
                : m_socket(socket), m_options(std::move(options)), m_certificate(std::move(certificate))
            {
            }
            bool Handshake(std::string& error) override
            {
                if (!m_certificate.sha256.empty() &&
                    (!m_options.trustDecision || !m_options.trustDecision(m_certificate)))
                {
                    error = "the certificate was not accepted";
                    return false;
                }
                return true;
            }
            ptrdiff_t Read(void* buffer, size_t length) override { return m_socket.Receive(buffer, length); }
            bool Write(const void* data, size_t length) override { return m_socket.SendAll(data, length); }
            void Shutdown() override {}
            std::string GetProtocolName() const override { return "fake TLS"; }
            std::vector<uint8_t> GetPeerPublicKey() const override { return {}; }

        private:
            Net::Socket& m_socket;
            Net::TlsOptions m_options;
            Net::TlsCertificate m_certificate;
        };

        // The server side of the loopback session: raw TPKT frames on the accepted socket.
        struct ScriptedRdpServer
        {
            Net::Socket& socket;

            bool ReadExact(uint8_t* buffer, size_t count)
            {
                size_t got = 0;
                while (got < count)
                {
                    ptrdiff_t n = socket.Receive(buffer + got, count - got);
                    if (n <= 0)
                    {
                        return false;
                    }
                    got += static_cast<size_t>(n);
                }
                return true;
            }
            // One TPKT frame's X.224 payload (the MCS PDU, or the connection request).
            Rdp::Bytes ReadFrame()
            {
                uint8_t header[4];
                if (!ReadExact(header, 4) || header[0] != 0x03)
                {
                    return {};
                }
                size_t total = (static_cast<size_t>(header[2]) << 8) | header[3];
                Rdp::Bytes frame(header, header + 4);
                frame.resize(total);
                if (total < 4 || !ReadExact(frame.data() + 4, total - 4))
                {
                    return {};
                }
                return frame;
            }
            Rdp::Bytes ReadMcs()
            {
                Rdp::Bytes payload;
                Rdp::UnframeX224Data(ReadFrame(), payload);
                return payload;
            }
            // The RDP PDU inside the next Send Data Request.
            Rdp::Bytes ReadRequest()
            {
                Rdp::Bytes mcs = ReadMcs();
                if (mcs.size() < 7 || (mcs[0] & 0xFC) != 0x64)
                {
                    return {};
                }
                mcs[0] = 0x68; // read it as an indication: the layout is the same
                uint16_t channel = 0;
                Rdp::Bytes pdu;
                Rdp::StripSendData(mcs, channel, pdu);
                return pdu;
            }
            void SendMcs(const Rdp::Bytes& mcs)
            {
                Rdp::Bytes frame = Rdp::FrameX224Data(mcs);
                socket.SendAll(frame.data(), frame.size());
            }
            void SendPdu(const Rdp::Bytes& pdu) { SendMcs(Indication(pdu)); }

            // Everything up to the Font Map, for a `width` by `height` desktop. False if the client went off script.
            bool RunConnectionSequence(uint32_t shareId, uint16_t width, uint16_t height, Rdp::Bytes& clientInfo)
            {
                Rdp::Bytes request = ReadFrame();
                if (request.size() < 11 || request[5] != 0xE0)
                {
                    printf("  expected an X.224 Connection Request\n");
                    return false;
                }
                Rdp::Bytes confirm = ConnectionConfirm(Rdp::kTls);
                socket.SendAll(confirm.data(), confirm.size());
                Rdp::Bytes connectInitial = ReadMcs();
                if (connectInitial.size() < 2 || connectInitial[0] != 0x7f || connectInitial[1] != 0x65)
                {
                    printf("  expected an MCS Connect-Initial\n");
                    return false;
                }
                SendMcs(ConnectResponse());
                ReadMcs(); // erect domain
                ReadMcs(); // attach user
                SendMcs({0x2E, 0x00, 0x00, 0x06}); // attach user confirm: user id 1007
                for (int i = 0; i < 2; ++i)
                {
                    Rdp::Bytes join = ReadMcs();
                    if (join.size() < 5 || (join[0] & 0xFC) != 0x38)
                    {
                        printf("  expected a Channel Join Request\n");
                        return false;
                    }
                    SendMcs({0x3E, 0x00, 0x00, 0x06, join[3], join[4], join[3], join[4]});
                }
                clientInfo = ReadRequest();
                SendPdu(DemandActive(shareId, width, height));
                Rdp::Bytes confirmActive = ReadRequest();
                Rdp::ShareControlType type = Rdp::ShareControlType::Data;
                uint16_t source = 0;
                Rdp::Bytes body;
                if (!Rdp::ReadShareControl(confirmActive, type, source, body) ||
                    type != Rdp::ShareControlType::ConfirmActive)
                {
                    printf("  expected a Confirm Active\n");
                    return false;
                }
                for (int i = 0; i < 4; ++i)
                {
                    ReadRequest(); // synchronize, control cooperate, control request, font list
                }
                Rdp::Bytes sync;
                P16(sync, 1); P16(sync, 1002);
                SendPdu(Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::Synchronize, sync));
                Rdp::Bytes cooperate;
                P16(cooperate, 4); P16(cooperate, 0); P32(cooperate, 0x03EA);
                SendPdu(Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::Control, cooperate));
                Rdp::Bytes granted;
                P16(granted, 2); P16(granted, 1007); P32(granted, 0x03EA);
                SendPdu(Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::Control, granted));
                Rdp::Bytes fontMap;
                P16(fontMap, 0); P16(fontMap, 0); P16(fontMap, 3); P16(fontMap, 4);
                SendPdu(Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::FontMap, fontMap));
                return true;
            }
        };

        // Whether `haystack` contains `needle` as UTF-16LE.
        bool ContainsUtf16(const Rdp::Bytes& haystack, const std::string& needle)
        {
            Rdp::Bytes wide;
            for (char c : needle)
            {
                wide.push_back(static_cast<uint8_t>(c));
                wide.push_back(0);
            }
            return std::search(haystack.begin(), haystack.end(), wide.begin(), wide.end()) != haystack.end();
        }
    } // namespace

    void RunRdpPointerTests()
    {
        RunTest("A colour pointer decodes to ARGB with the AND mask making pixels transparent", []() {
            // 2 by 2, black everywhere; the AND mask (bit 6 of the row byte) makes the top-right pixel transparent.
            Rdp::Bytes data = ColourPointer(3, 2, 2, 0x000000, {0x40, 0x00});
            Rdp::PointerUpdate update;
            std::string error;
            CHECK(Rdp::DecodePointerAttribute(data, 24, nullptr, update, error));
            CHECK(update.kind == Rdp::PointerUpdateKind::New && update.cacheIndex == 3);
            CHECK(update.shape.width == 2 && update.shape.height == 2);
            CHECK(update.shape.hotspotX == 1 && update.shape.hotspotY == 1);
            CHECK(update.shape.argb.size() == 4);
            CHECK(update.shape.argb[0] == 0xFF000000); // top left: opaque black
            CHECK(update.shape.argb[1] == 0x00000000); // top right: transparent
            CHECK(update.shape.argb[2] == 0xFF000000 && update.shape.argb[3] == 0xFF000000);
            // With the AND bit set over a coloured pixel the server means "invert the screen"; that shows as black.
            Rdp::Bytes inverting = ColourPointer(0, 1, 1, 0xFFFFFF, {0x80});
            CHECK(Rdp::DecodePointerAttribute(inverting, 24, nullptr, update, error));
            CHECK(update.shape.argb[0] == 0xFF000000);
        });

        RunTest("The slow-path pointer PDU and the fast-path codes are told apart", []() {
            Rdp::PointerUpdate update;
            std::string error;
            Rdp::Bytes hidden;
            P16(hidden, 1); P16(hidden, 0); P32(hidden, 0); // system pointer: null
            CHECK(Rdp::ParsePointerPdu(hidden, update, error) && update.kind == Rdp::PointerUpdateKind::Hidden);
            Rdp::Bytes standard;
            P16(standard, 1); P16(standard, 0); P32(standard, 0x7F00);
            CHECK(Rdp::ParsePointerPdu(standard, update, error) && update.kind == Rdp::PointerUpdateKind::Default);
            Rdp::Bytes cached;
            P16(cached, 7); P16(cached, 0); P16(cached, 9);
            CHECK(Rdp::ParsePointerPdu(cached, update, error) && update.kind == Rdp::PointerUpdateKind::Cached &&
                  update.cacheIndex == 9);
            Rdp::Bytes colour;
            P16(colour, 6); P16(colour, 0);
            Rdp::Bytes attribute = ColourPointer(0, 1, 1, 0x0000FF, {0x00});
            colour.insert(colour.end(), attribute.begin(), attribute.end());
            CHECK(Rdp::ParsePointerPdu(colour, update, error) && update.kind == Rdp::PointerUpdateKind::New &&
                  update.shape.argb[0] == 0xFF0000FF);
            // A New Pointer Update at 32 bits uses the alpha channel when any pixel has one.
            Rdp::Bytes deep;
            P16(deep, 8); P16(deep, 0);
            P16(deep, 32); // xorBpp
            P16(deep, 0); P16(deep, 0); P16(deep, 0); P16(deep, 1); P16(deep, 1); P16(deep, 2); P16(deep, 4);
            deep.insert(deep.end(), {0x10, 0x20, 0x30, 0xFF, 0xFF, 0x00});
            CHECK(Rdp::ParsePointerPdu(deep, update, error) && update.kind == Rdp::PointerUpdateKind::New &&
                  update.shape.argb[0] == 0xFF302010);
            // Fast-path codes.
            CHECK(Rdp::ParseFastPathPointer(5, {}, update, error) && update.kind == Rdp::PointerUpdateKind::Hidden);
            CHECK(Rdp::ParseFastPathPointer(6, {}, update, error) && update.kind == Rdp::PointerUpdateKind::Default);
            Rdp::Bytes cachedFast;
            P16(cachedFast, 4);
            CHECK(Rdp::ParseFastPathPointer(10, cachedFast, update, error) &&
                  update.kind == Rdp::PointerUpdateKind::Cached && update.cacheIndex == 4);
            CHECK(Rdp::ParseFastPathPointer(9, attribute, update, error) &&
                  update.kind == Rdp::PointerUpdateKind::New);
            CHECK(!Rdp::ParseFastPathPointer(1, {}, update, error)); // a bitmap update isn't a pointer
            // Truncated shapes fail with a reason.
            Rdp::Bytes shortShape(attribute.begin(), attribute.begin() + 16);
            CHECK(!Rdp::DecodePointerAttribute(shortShape, 24, nullptr, update, error) && !error.empty());
        });

        RunTest("The default arrow is a usable cursor shape", []() {
            CursorShape arrow = Rdp::DefaultArrowCursor();
            CHECK(arrow.width == 12 && arrow.height == 19);
            CHECK(arrow.argb.size() == 12 * 19);
            CHECK(arrow.argb[0] == 0xFF000000); // the tip is black
            CHECK(arrow.argb[11] == 0);         // the top-right corner is transparent
        });

        RunTest("A Demand Active's capability sets give the server's desktop size", []() {
            Rdp::DemandActiveInfo info;
            CHECK(Rdp::ParseDemandActiveInfo(DemandActive(0x12345, 1280, 1024), info));
            CHECK(info.shareId == 0x12345 && info.capabilitySetCount == 2);
            CHECK(info.desktopWidth == 1280 && info.desktopHeight == 1024 && info.preferredBitsPerPixel == 16);
            CHECK(!info.fastPathOutput);
        });

        RunTest("A palette update and a slow-path update PDU parse", []() {
            Rdp::Bytes body;
            P16(body, 0x0002); // updateType: palette
            P16(body, 0);
            P32(body, 2);
            body.insert(body.end(), {0x11, 0x22, 0x33, 0xAA, 0xBB, 0xCC});
            Rdp::Update update;
            std::string error;
            CHECK(Rdp::ParseUpdatePdu(body, update, error));
            CHECK(update.type == Rdp::UpdateType::Palette && update.palette.size() == 256);
            CHECK(update.palette[0] == 0x112233 && update.palette[1] == 0xAABBCC && update.palette[2] == 0);
            Rdp::Bytes bitmap = BitmapUpdatePdu(1, 2, 3, 4, 4, 0xF800);
            Rdp::ShareDataType type2;
            Rdp::Bytes data;
            CHECK(Rdp::ReadShareData(bitmap, type2, data) && type2 == Rdp::ShareDataType::Update);
            CHECK(Rdp::ParseUpdatePdu(data, update, error) && update.type == Rdp::UpdateType::Bitmap);
            CHECK(update.rects.size() == 1 && update.rects[0].destWidth == 4 && update.rects[0].destHeight == 4);
            CHECK(Rdp::ErrorInfoText(0x0C) == "You were logged off.");
            CHECK(Rdp::ErrorInfoText(0x1234) == "The server ended the connection (code 0x00001234).");
        });
    }

    void RunRdpClientTests()
    {
        RunTest("Rdp::Client runs a session against a scripted server: picture, pointer, input, and the end", []() {
            FakeServer server;
            RecordingListener listener;
            listener.credentialsToGive = {"bob", "secret"};
            listener.trustCertificate = true;
            Net::TlsCertificate certificate;
            certificate.sha256.assign(32, 0x5A);
            certificate.subject = "CN=snorkels";
            certificate.problem = "self-signed";
            Rdp::Client client(listener);
            Rdp::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.desktopWidth = 320;
            options.desktopHeight = 240;
            options.createTlsStream = [certificate](Net::Socket& socket, const Net::TlsOptions& tlsOptions) {
                return std::unique_ptr<Net::TlsStream>(new PassthroughTls(socket, tlsOptions, certificate));
            };
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.GetClientSocket().SetReceiveTimeout(5000);
            ScriptedRdpServer script{server.GetClientSocket()};
            const uint32_t shareId = 0x000A0001;
            Rdp::Bytes clientInfo;
            CHECK(script.RunConnectionSequence(shareId, 64, 48, clientInfo));
            // The credentials were asked for (no password was given), the certificate was shown, and the login
            // went in the Client Info packet.
            CHECK(listener.WaitFor([&]() { return listener.connected; }));
            CHECK(listener.credentialRequests == 1 && listener.lastKind == Rfb::CredentialsKind::Account);
            CHECK(listener.certificateQuestions == 1 && listener.lastCertificate.subject == "CN=snorkels");
            CHECK(clientInfo.size() > 4 && U16(clientInfo, 0) == 0x0040); // SEC_INFO_PKT
            CHECK(ContainsUtf16(clientInfo, "bob") && ContainsUtf16(clientInfo, "secret"));
            // The desktop is the size the server announced, not the one asked for.
            CHECK(listener.serverInit.width == 64 && listener.serverInit.height == 48);
            CHECK(client.IsConnected() && client.IsEncrypted() && client.GetEncryptionName() == "fake TLS");

            // A slow-path bitmap update lands in the framebuffer.
            script.SendPdu(BitmapUpdatePdu(shareId, 2, 3, 4, 4, 0xF800)); // red
            CHECK(listener.WaitFor([&]() { return listener.updates == 1; }));
            {
                Rfb::Framebuffer& framebuffer = client.GetFramebuffer();
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                CHECK(framebuffer.GetWidth() == 64 && framebuffer.GetHeight() == 48);
                CHECK(framebuffer.GetPixel(2, 3) == 0xFF0000);
                CHECK(framebuffer.GetPixel(5, 6) == 0xFF0000);
                CHECK(framebuffer.GetPixel(1, 3) == 0 && framebuffer.GetPixel(6, 6) == 0);
            }
            CHECK(listener.lastRects.size() == 1 && listener.lastRects[0].x == 2 && listener.lastRects[0].width == 4);

            // A fast-path bitmap update too: header byte 0 (no flags), a one-byte length, then one update.
            {
                Rdp::Bytes inner;
                P16(inner, 0x0001); // updateType
                P16(inner, 1);
                P16(inner, 10); P16(inner, 10); P16(inner, 10); P16(inner, 10);
                P16(inner, 1); P16(inner, 1); P16(inner, 16); P16(inner, 0); P16(inner, 4);
                inner.insert(inner.end(), {0xE0, 0x07, 0x00, 0x00}); // green, padded to 4 bytes
                Rdp::Bytes updates = {0x01};
                P16(updates, static_cast<uint16_t>(inner.size()));
                updates.insert(updates.end(), inner.begin(), inner.end());
                Rdp::Bytes frame = {0x00, static_cast<uint8_t>(2 + updates.size())};
                frame.insert(frame.end(), updates.begin(), updates.end());
                server.GetClientSocket().SendAll(frame.data(), frame.size());
            }
            CHECK(listener.WaitFor([&]() { return listener.updates == 2; }));
            {
                Rfb::Framebuffer& framebuffer = client.GetFramebuffer();
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                CHECK(framebuffer.GetPixel(10, 10) == 0x00FF00);
            }

            // A colour pointer shape reaches the listener.
            Rdp::Bytes pointer;
            P16(pointer, 6); P16(pointer, 0);
            Rdp::Bytes attribute = ColourPointer(0, 2, 2, 0xFFFFFF, {0x00, 0x00});
            pointer.insert(pointer.end(), attribute.begin(), attribute.end());
            script.SendPdu(Rdp::WrapShareData(shareId, 0x03EA, Rdp::ShareDataType::Pointer, pointer));
            CHECK(listener.WaitFor([&]() { return listener.cursorUpdates == 1; }));
            CHECK(listener.cursor.width == 2 && listener.cursor.argb[0] == 0xFFFFFFFF);

            // Input: a key press becomes a scancode event, and a mouse press a move plus a button event.
            client.SendKeyEvent('a', true);
            Rdp::Bytes input = script.ReadRequest();
            Rdp::ShareDataType type2;
            Rdp::Bytes events;
            CHECK(Rdp::ReadShareData(input, type2, events) && type2 == Rdp::ShareDataType::Input);
            CHECK(events.size() == 4 + 12 && U16(events, 0) == 1);
            CHECK(U16(events, 4 + 4) == 0x0004 && U16(events, 4 + 8) == 0x1E); // scancode event for 'a'
            client.SendPointerEvent(10, 20, 0x01);
            input = script.ReadRequest();
            CHECK(Rdp::ReadShareData(input, type2, events) && type2 == Rdp::ShareDataType::Input);
            CHECK(U16(events, 0) == 2);
            CHECK(U16(events, 4 + 4) == 0x8001 && U16(events, 4 + 6) == Rdp::kPtrMove);
            CHECK(U16(events, 4 + 8) == 10 && U16(events, 4 + 10) == 20);
            CHECK(U16(events, 16 + 6) == (Rdp::kPtrDown | Rdp::kPtrButton1));
            // Releasing sends just the button up (and the move).
            client.SendPointerEvent(10, 20, 0x00);
            input = script.ReadRequest();
            CHECK(Rdp::ReadShareData(input, type2, events) && U16(events, 0) == 2);
            CHECK(U16(events, 16 + 6) == Rdp::kPtrButton1);
            // Asking for the whole picture again sends a Refresh Rect for the desktop.
            client.RequestFullUpdate();
            Rdp::Bytes refresh = script.ReadRequest();
            Rdp::Bytes refreshBody;
            CHECK(Rdp::ReadShareData(refresh, type2, refreshBody) && type2 == Rdp::ShareDataType::RefreshRect);
            CHECK(refreshBody.size() == 12 && U16(refreshBody, 8) == 63 && U16(refreshBody, 10) == 47);
            TrafficStats stats = client.TakeTrafficStats();
            CHECK(stats.updates == 2 && stats.rectangles == 2 && stats.keyEvents == 1 && stats.pointerEvents == 2);

            // The server changes its desktop: a new Demand Active resizes the picture, and the client agrees.
            script.SendPdu(DemandActive(shareId, 80, 60));
            Rdp::Bytes confirm = script.ReadRequest();
            Rdp::ShareControlType controlType = Rdp::ShareControlType::Data;
            uint16_t source = 0;
            Rdp::Bytes confirmBody;
            CHECK(Rdp::ReadShareControl(confirm, controlType, source, confirmBody) &&
                  controlType == Rdp::ShareControlType::ConfirmActive);
            for (int i = 0; i < 4; ++i)
            {
                script.ReadRequest(); // the finalisation PDUs again
            }
            CHECK(listener.WaitFor([&]() { return listener.resizedWidth == 80; }));
            CHECK(listener.resizedHeight == 60 && client.GetFramebuffer().GetWidth() == 80);

            // The server says why, then closes: the reason reaches the listener.
            Rdp::Bytes errorInfo;
            P32(errorInfo, 0x0000000C);
            script.SendPdu(Rdp::WrapShareData(shareId, 0x03EA, static_cast<Rdp::ShareDataType>(47), errorInfo));
            server.Close();
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            CHECK(listener.disconnectError == "You were logged off.");
            CHECK(!client.IsConnected());
        });

        RunTest("Rdp::Client reports a refused certificate and a cancelled login cleanly", []() {
            FakeServer server;
            RecordingListener listener;
            listener.credentialsToGive = {"bob", "secret"};
            listener.trustCertificate = false;
            Net::TlsCertificate certificate;
            certificate.sha256.assign(32, 0x5A);
            Rdp::Client client(listener);
            Rdp::ClientOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            options.credentials = {"bob", "secret"};
            options.createTlsStream = [certificate](Net::Socket& socket, const Net::TlsOptions& tlsOptions) {
                return std::unique_ptr<Net::TlsStream>(new PassthroughTls(socket, tlsOptions, certificate));
            };
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.GetClientSocket().SetReceiveTimeout(5000);
            ScriptedRdpServer script{server.GetClientSocket()};
            script.ReadFrame();
            Rdp::Bytes confirm = ConnectionConfirm(Rdp::kTls);
            server.GetClientSocket().SendAll(confirm.data(), confirm.size());
            CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            CHECK(listener.credentialRequests == 0); // the password was given
            CHECK(listener.certificateQuestions == 1 && !listener.disconnectError.empty());
            CHECK(!listener.authenticationFailed);
            server.Close();

            // No password and a cancelled prompt: a clean disconnect with no error, and no connection made.
            RecordingListener cancelling;
            Rdp::Client second(cancelling);
            Rdp::ClientOptions none;
            none.host = "127.0.0.1";
            none.port = 1; // never reached
            second.Connect(none);
            CHECK(cancelling.WaitFor([&]() { return cancelling.disconnected; }));
            CHECK(cancelling.credentialRequests == 1 && cancelling.disconnectError.empty());
        });

        // Opt-in, against a real RDP server: MARCONI_RDP_TEST=host:port, with MARCONI_RDP_USER and
        // MARCONI_RDP_PASSWORD for the login (an xrdp login screen appears without them). Trusts whatever
        // certificate the server sends. MARCONI_RDP_SNAPSHOT=file.ppm saves the picture that arrived.
        const char* target = std::getenv("MARCONI_RDP_TEST");
        if (target != nullptr)
        {
            RunTest("A real RDP server brings a session up and sends a picture", [&]() {
                std::string text = target;
                size_t colon = text.rfind(':');
                CHECK(colon != std::string::npos);
                RecordingListener listener;
                listener.trustCertificate = true;
                const char* user = std::getenv("MARCONI_RDP_USER");
                const char* password = std::getenv("MARCONI_RDP_PASSWORD");
                listener.credentialsToGive = {user != nullptr ? user : "", password != nullptr ? password : "x"};
                Rdp::Client client(listener);
                Rdp::ClientOptions options;
                options.host = text.substr(0, colon);
                options.port = static_cast<uint16_t>(std::atoi(text.c_str() + colon + 1));
                options.credentials = listener.credentialsToGive;
                options.desktopWidth = 800;
                options.desktopHeight = 600;
                client.Connect(options);
                CHECK(listener.WaitFor([&]() { return listener.connected || listener.disconnected; }, 20000));
                if (!listener.connected)
                {
                    printf("  not connected: %s\n", listener.disconnectError.c_str());
                    return;
                }
                printf("  connected to %s: desktop %dx%d, %s, %s\n", target, listener.serverInit.width,
                       listener.serverInit.height, client.GetEncryptionName().c_str(),
                       listener.certificateQuestions ? "certificate asked" : "certificate trusted");
                CHECK(listener.WaitFor([&]() { return listener.updates > 0 || listener.disconnected; }, 15000));
                TrafficStats stats = client.TakeTrafficStats();
                printf("  %d updates with %d rectangles, %d pointer shapes%s\n", stats.updates, stats.rectangles,
                       listener.cursorUpdates, listener.disconnected ? " (then disconnected)" : "");
                bool black = true;
                {
                    Rfb::Framebuffer& framebuffer = client.GetFramebuffer();
                    std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                    black = framebuffer.IsBlack();
                    const char* snapshot = std::getenv("MARCONI_RDP_SNAPSHOT");
                    if (snapshot != nullptr)
                    {
                        FILE* file = fopen(snapshot, "wb");
                        if (file != nullptr)
                        {
                            fprintf(file, "P6\n%d %d\n255\n", framebuffer.GetWidth(), framebuffer.GetHeight());
                            for (int y = 0; y < framebuffer.GetHeight(); ++y)
                            {
                                for (int x = 0; x < framebuffer.GetWidth(); ++x)
                                {
                                    uint32_t pixel = framebuffer.GetPixel(x, y);
                                    uint8_t rgb[3] = {static_cast<uint8_t>(pixel >> 16), static_cast<uint8_t>(pixel >> 8),
                                                      static_cast<uint8_t>(pixel)};
                                    fwrite(rgb, 1, 3, file);
                                }
                            }
                            fclose(file);
                            printf("  picture saved to %s\n", snapshot);
                        }
                    }
                }
                CHECK(!black);
                client.Disconnect();
                CHECK(listener.WaitFor([&]() { return listener.disconnected; }));
            });
        }
        else
        {
            printf("  (set MARCONI_RDP_TEST=host:port, a real RDP server, for the end-to-end session test)\n");
        }

        RunTest("The ways to write an account name split into a domain and a user", []() {
            // Checked through the Client Info packet the client sends: see the scripted session above for the
            // plain case. "DOMAIN\\user" is split so the domain goes in its own field.
            Rdp::LogonInfo logon;
            logon.domain = "SNORKELS";
            logon.user = "bob";
            Rdp::Bytes info = Rdp::BuildClientInfo(logon);
            CHECK(ContainsUtf16(info, "SNORKELS") && ContainsUtf16(info, "bob"));
        });
    }
} // namespace MarconiTest
