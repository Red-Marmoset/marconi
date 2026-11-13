// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for Marconi for AI tools: the MCP messages, PNG writing, scaling, and connect/screenshot against a fake server

#include "TestSupport.h"

#include <MarconiMcp/Base64.h>
#include <MarconiMcp/Connection.h>
#include <MarconiMcp/Input.h>
#include <MarconiMcp/Picture.h>
#include <MarconiMcp/Png.h>
#include <MarconiMcp/Protocol.h>
#include <MarconiMcp/Tools.h>

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/Rfb/Messages.h>

#include <zlib.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;
    using nlohmann::json;

    namespace
    {
        // A tool set with one tool, "echo", that returns its "text" argument, and one that always fails.
        class EchoTools : public Mcp::ToolSet
        {
        public:
            std::vector<Mcp::ToolDefinition> ListTools() const override
            {
                Mcp::ToolDefinition echo;
                echo.name = "echo";
                echo.description = "Says it back.";
                echo.inputSchema = {{"type", "object"}};
                echo.readOnly = true;
                return {echo};
            }

            bool CallTool(const std::string& name, const json& arguments, Mcp::ToolResult& out) override
            {
                if (name != "echo")
                {
                    return false;
                }
                out.AddText(arguments.value("text", std::string("(nothing)")));
                return true;
            }
        };

        json Call(Mcp::Server& server, const json& message)
        {
            const std::string reply = server.HandleMessage(message.dump());
            if (reply.empty())
            {
                return json();
            }
            CHECK(reply.find('\n') == std::string::npos);
            return json::parse(reply);
        }

        json ModernMeta(const std::string& version = "2026-07-28")
        {
            return {{"io.modelcontextprotocol/protocolVersion", version},
                    {"io.modelcontextprotocol/clientCapabilities", json::object()},
                    {"io.modelcontextprotocol/clientInfo", {{"name", "test"}, {"version", "1"}}}};
        }

        uint32_t BigEndian32(const uint8_t* p)
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8) | p[3];
        }

        // Just enough PNG reading to check what EncodePng writes: 8-bit RGB, one IDAT, per-row filters 0 and 1.
        bool DecodePng(const std::vector<uint8_t>& png, Mcp::Picture& out)
        {
            static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
            if (png.size() < 8 || std::memcmp(png.data(), kSignature, 8) != 0)
            {
                return false;
            }
            size_t at = 8;
            std::vector<uint8_t> idat;
            bool sawEnd = false;
            while (at + 12 <= png.size())
            {
                const uint32_t length = BigEndian32(&png[at]);
                const std::string type(reinterpret_cast<const char*>(&png[at + 4]), 4);
                if (at + 12 + length > png.size())
                {
                    return false;
                }
                const uint8_t* data = &png[at + 8];
                uLong crc = crc32(0L, Z_NULL, 0);
                crc = crc32(crc, &png[at + 4], 4 + length);
                if (static_cast<uint32_t>(crc) != BigEndian32(data + length))
                {
                    return false;
                }
                if (type == "IHDR")
                {
                    out.width = static_cast<int>(BigEndian32(data));
                    out.height = static_cast<int>(BigEndian32(data + 4));
                    if (data[8] != 8 || data[9] != 2)
                    {
                        return false;
                    }
                }
                else if (type == "IDAT")
                {
                    idat.insert(idat.end(), data, data + length);
                }
                else if (type == "IEND")
                {
                    sawEnd = true;
                }
                at += 12 + length;
            }
            if (!sawEnd || out.width <= 0 || out.height <= 0)
            {
                return false;
            }
            const size_t rowBytes = 1 + static_cast<size_t>(out.width) * 3;
            std::vector<uint8_t> raw(rowBytes * out.height);
            uLongf rawSize = static_cast<uLongf>(raw.size());
            if (uncompress(raw.data(), &rawSize, idat.data(), static_cast<uLong>(idat.size())) != Z_OK ||
                rawSize != raw.size())
            {
                return false;
            }
            out.pixels.assign(static_cast<size_t>(out.width) * out.height, 0);
            for (int y = 0; y < out.height; ++y)
            {
                uint8_t* row = raw.data() + y * rowBytes;
                if (row[0] == 1)
                {
                    for (size_t i = 4; i < rowBytes; ++i)
                    {
                        row[i] = static_cast<uint8_t>(row[i] + row[i - 3]);
                    }
                }
                else if (row[0] != 0)
                {
                    return false;
                }
                for (int x = 0; x < out.width; ++x)
                {
                    const uint8_t* p = row + 1 + x * 3;
                    out.pixels[static_cast<size_t>(y) * out.width + x] =
                        (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                }
            }
            return true;
        }

        std::vector<uint8_t> Base64Decode(const std::string& text)
        {
            static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<uint8_t> out;
            uint32_t buffer = 0;
            int bits = 0;
            for (char c : text)
            {
                if (c == '=')
                {
                    break;
                }
                const size_t value = kAlphabet.find(c);
                if (value == std::string::npos)
                {
                    throw std::runtime_error("not base64");
                }
                buffer = (buffer << 6) | static_cast<uint32_t>(value);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>(buffer >> bits));
                }
            }
            return out;
        }

        void RectHeader(Rfb::OutBuffer& out, int x, int y, int w, int h, int32_t encoding)
        {
            out.U16(static_cast<uint16_t>(x)).U16(static_cast<uint16_t>(y));
            out.U16(static_cast<uint16_t>(w)).U16(static_cast<uint16_t>(h)).S32(encoding);
        }

        // A MarconiTools connected to a fake VNC server of the given size.
        struct ConnectedTools
        {
            FakeServer server;
            Mcp::MarconiTools tools{Mcp::ToolOptions{}};

            ConnectedTools(int width, int height)
            {
                Mcp::ToolResult result;
                std::thread connecting([&]() {
                    tools.CallTool("connect", {{"address", "127.0.0.1::" + std::to_string(server.GetPort())}}, result);
                });
                const bool accepted = server.AcceptClient();
                if (accepted)
                {
                    server.HandshakeNone("RFB 003.008\n", width, height, "fake desktop");
                    server.ReadClientSetup();
                }
                connecting.join();
                if (!accepted || result.isError)
                {
                    throw std::runtime_error("couldn't connect to the fake server");
                }
            }

            // Paints the desktop one colour, so a screenshot can be taken.
            void Paint(int width, int height)
            {
                Rfb::OutBuffer update;
                update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
                RectHeader(update, 0, 0, width, height, Rfb::Encoding::Raw);
                const uint32_t grey = 0x00808080u;
                for (int i = 0; i < width * height; ++i)
                {
                    update.Append(&grey, 4);
                }
                server.Send(update);
            }

            Mcp::ToolResult Call(const std::string& name, const json& arguments = json::object())
            {
                Mcp::ToolResult result;
                CHECK(tools.CallTool(name, arguments, result));
                return result;
            }
        };

        // What the client sent, in order: pointer events as {'P', mask, x, y}, key events as {'K', down, keysym, 0},
        // cut text as {'C', length, 0, 0} with the text in `cutText`. Update requests are skipped.
        struct Event
        {
            char kind = 0;
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t c = 0;
        };

        std::vector<Event> ReadEvents(FakeServer& server, size_t count, std::string* cutText = nullptr)
        {
            std::vector<Event> events;
            while (events.size() < count)
            {
                const uint8_t type = server.In().U8();
                if (type == Rfb::ClientMessage::FramebufferUpdateRequest)
                {
                    server.In().Skip(9);
                }
                else if (type == Rfb::ClientMessage::PointerEvent)
                {
                    Event event;
                    event.kind = 'P';
                    event.a = server.In().U8();
                    event.b = server.In().U16();
                    event.c = server.In().U16();
                    events.push_back(event);
                }
                else if (type == Rfb::ClientMessage::KeyEvent)
                {
                    Event event;
                    event.kind = 'K';
                    event.a = server.In().U8();
                    server.In().Skip(2);
                    event.b = server.In().U32();
                    events.push_back(event);
                }
                else if (type == Rfb::ClientMessage::ClientCutText)
                {
                    server.In().Skip(3);
                    Event event;
                    event.kind = 'C';
                    event.a = server.In().U32();
                    const std::string text = server.In().ReadString(event.a);
                    if (cutText)
                    {
                        *cutText = text;
                    }
                    events.push_back(event);
                }
                else
                {
                    throw std::runtime_error("unexpected client message " + std::to_string(type));
                }
            }
            return events;
        }
    } // namespace

    void RunMcpTests()
    {
        RunTest("MCP: base64", []() {
            CHECK(Mcp::Base64Encode({}) == "");
            CHECK(Mcp::Base64Encode({'f'}) == "Zg==");
            CHECK(Mcp::Base64Encode({'f', 'o'}) == "Zm8=");
            CHECK(Mcp::Base64Encode({'f', 'o', 'o'}) == "Zm9v");
            CHECK(Mcp::Base64Encode({'f', 'o', 'o', 'b', 'a', 'r'}) == "Zm9vYmFy");
            CHECK(Mcp::Base64Encode({0xff, 0xfe, 0xfd}) == "//79");
        });

        RunTest("MCP: screenshot sizes stay within the limits and keep their shape", []() {
            Mcp::PictureSize size = Mcp::FitWithin(1920, 1080, Mcp::kDefaultMaxSide, Mcp::kDefaultMaxPixels);
            CHECK(size.width <= 1568 && size.height <= 1568);
            CHECK(static_cast<long long>(size.width) * size.height <= Mcp::kDefaultMaxPixels);
            CHECK(size.width >= 1400); // not shrunk more than needed
            CHECK(std::abs(size.width * 1080 - size.height * 1920) <= 1920);

            size = Mcp::FitWithin(800, 600, Mcp::kDefaultMaxSide, Mcp::kDefaultMaxPixels);
            CHECK(size.width == 800 && size.height == 600); // small pictures are left alone

            size = Mcp::FitWithin(5120, 1440, Mcp::kDefaultMaxSide, Mcp::kDefaultMaxPixels);
            CHECK(size.width == 1568); // a wide screen is limited by its long side
            CHECK(size.height == 441);

            size = Mcp::FitWithin(3840, 2160, Mcp::kLargestMaxSide, Mcp::kLargestMaxPixels);
            CHECK(size.width <= 2576 && static_cast<long long>(size.width) * size.height <= 3750000);
            CHECK(size.width > 2500);

            size = Mcp::FitWithin(1, 10000, 100, 0);
            CHECK(size.width == 1 && size.height == 100);
        });

        RunTest("MCP: scaling down averages the pixels each one covers", []() {
            Mcp::Picture source;
            source.width = 2;
            source.height = 2;
            source.pixels = {0x00000000u, 0x00ffffffu, 0x00ffffffu, 0x00000000u};
            Mcp::Picture one = Mcp::ScaleDown(source, 1, 1);
            CHECK(one.width == 1 && one.height == 1);
            CHECK(one.pixels.size() == 1 && one.pixels[0] == 0x00808080u);

            Mcp::Picture same = Mcp::ScaleDown(source, 5, 5); // never enlarged
            CHECK(same.width == 2 && same.height == 2 && same.pixels == source.pixels);
        });

        RunTest("MCP: copying an area and drawing the cursor", []() {
            std::vector<uint32_t> desktop(4 * 3, 0x00111111u);
            desktop[1 * 4 + 2] = 0x00abcdefu;
            Mcp::Picture area = Mcp::CopyArea(desktop.data(), 4, 1, 1, 2, 2);
            CHECK(area.width == 2 && area.height == 2);
            CHECK(area.pixels[1] == 0x00abcdefu);

            CursorShape cursor;
            cursor.width = 2;
            cursor.height = 1;
            cursor.hotspotX = 1;
            cursor.argb = {0xff00ff00u, 0x00ff0000u}; // a visible green pixel and a transparent one
            Mcp::DrawCursor(area, cursor, 1, 0);      // hotspot on the second pixel: the green one lands at 0,0
            CHECK(area.pixels[0] == 0x0000ff00u);
            CHECK(area.pixels[1] == 0x00abcdefu);
            Mcp::DrawCursor(area, cursor, -10, -10); // entirely outside: nothing happens
            CHECK(area.pixels[0] == 0x0000ff00u);
        });

        RunTest("MCP: PNG round trip", []() {
            const int width = 7;
            const int height = 5;
            std::vector<uint32_t> pixels(width * height);
            for (int i = 0; i < width * height; ++i)
            {
                pixels[i] = static_cast<uint32_t>(i * 0x010305 + 0x00402010) & 0x00ffffffu;
            }
            Mcp::Picture decoded;
            CHECK(DecodePng(Mcp::EncodePng(pixels.data(), width, height, width), decoded));
            CHECK(decoded.width == width && decoded.height == height);
            CHECK(decoded.pixels == pixels);

            // A stride wider than the picture: only the first `width` pixels of each row are used.
            std::vector<uint32_t> wide(10 * height, 0x00ffffffu);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    wide[y * 10 + x] = pixels[y * width + x];
                }
            }
            CHECK(DecodePng(Mcp::EncodePng(wide.data(), width, height, 10), decoded));
            CHECK(decoded.pixels == pixels);
            CHECK(Mcp::EncodePng(pixels.data(), 0, 5, 0).empty());
        });

        RunTest("MCP: initialize answers with the client's version when it is one we speak", []() {
            EchoTools tools;
            Mcp::ServerInfo info;
            info.version = "1.2.3";
            info.instructions = "Hello.";
            Mcp::Server server(tools, info);
            json reply = Call(server, {{"jsonrpc", "2.0"},
                                       {"id", 1},
                                       {"method", "initialize"},
                                       {"params", {{"protocolVersion", "2025-06-18"},
                                                   {"capabilities", json::object()},
                                                   {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}});
            CHECK(reply["id"] == 1);
            CHECK(reply["result"]["protocolVersion"] == "2025-06-18");
            CHECK(reply["result"]["capabilities"].contains("tools"));
            CHECK(reply["result"]["serverInfo"]["name"] == "marconi");
            CHECK(reply["result"]["serverInfo"]["version"] == "1.2.3");
            CHECK(reply["result"]["instructions"] == "Hello.");

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", "two"},
                                  {"method", "initialize"},
                                  {"params", {{"protocolVersion", "2099-01-01"}}}});
            CHECK(reply["id"] == "two");
            CHECK(reply["result"]["protocolVersion"] == "2025-11-25");

            // The notification that follows needs no reply; nor does ping fail.
            CHECK(Call(server, {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}).is_null());
            reply = Call(server, {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "ping"}});
            CHECK(reply["result"].is_object() && reply["result"].empty());
        });

        RunTest("MCP: tools are listed and called", []() {
            EchoTools tools;
            Mcp::Server server(tools, Mcp::ServerInfo());
            json reply = Call(server, {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
            CHECK(reply["result"]["tools"].size() == 1);
            CHECK(reply["result"]["tools"][0]["name"] == "echo");
            CHECK(reply["result"]["tools"][0]["inputSchema"]["type"] == "object");
            CHECK(reply["result"]["tools"][0]["annotations"]["readOnlyHint"] == true);

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", 2},
                                  {"method", "tools/call"},
                                  {"params", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}}});
            CHECK(reply["result"]["isError"] == false);
            CHECK(reply["result"]["content"][0]["type"] == "text");
            CHECK(reply["result"]["content"][0]["text"] == "hi");
            CHECK(!reply["result"].contains("resultType")); // only the 2026 revision has it

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", 3},
                                  {"method", "tools/call"},
                                  {"params", {{"name", "nope"}}}});
            CHECK(reply["error"]["code"] == Mcp::kInvalidParams);

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", 4},
                                  {"method", "tools/call"},
                                  {"params", {{"name", "echo"}, {"arguments", "not an object"}}}});
            CHECK(reply["error"]["code"] == Mcp::kInvalidParams);
        });

        RunTest("MCP: malformed messages and unknown methods", []() {
            EchoTools tools;
            Mcp::Server server(tools, Mcp::ServerInfo());
            json reply = json::parse(server.HandleMessage("{not json"));
            CHECK(reply["error"]["code"] == Mcp::kParseError);
            CHECK(reply["id"].is_null());

            reply = json::parse(server.HandleMessage("[1, 2]"));
            CHECK(reply["error"]["code"] == Mcp::kInvalidRequest);

            reply = Call(server, {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "resources/list"}});
            CHECK(reply["error"]["code"] == Mcp::kMethodNotFound);

            // A reply from the client, and an unknown notification: nothing to say.
            CHECK(server.HandleMessage(R"({"jsonrpc":"2.0","id":5,"result":{}})").empty());
            CHECK(server.HandleMessage(R"({"jsonrpc":"2.0","method":"notifications/cancelled"})").empty());
        });

        RunTest("MCP: the 2026-07-28 revision, with the version in each request", []() {
            EchoTools tools;
            Mcp::ServerInfo info;
            info.version = "1.2.3";
            Mcp::Server server(tools, info);

            json reply = Call(server, {{"jsonrpc", "2.0"},
                                       {"id", "discover-1"},
                                       {"method", "server/discover"},
                                       {"params", {{"_meta", ModernMeta()}}}});
            CHECK(reply["result"]["resultType"] == "complete");
            CHECK(reply["result"]["supportedVersions"][0] == "2026-07-28");
            CHECK(reply["result"]["capabilities"].contains("tools"));
            CHECK(reply["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["version"] == "1.2.3");

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", 2},
                                  {"method", "tools/call"},
                                  {"params",
                                   {{"name", "echo"}, {"arguments", {{"text", "hi"}}}, {"_meta", ModernMeta()}}}});
            CHECK(reply["result"]["resultType"] == "complete");
            CHECK(reply["result"]["content"][0]["text"] == "hi");

            reply = Call(server, {{"jsonrpc", "2.0"},
                                  {"id", 3},
                                  {"method", "tools/list"},
                                  {"params", {{"_meta", ModernMeta("1900-01-01")}}}});
            CHECK(reply["error"]["code"] == Mcp::kUnsupportedProtocolVersion);
            CHECK(reply["error"]["data"]["requested"] == "1900-01-01");
            CHECK(reply["error"]["data"]["supported"][0] == "2026-07-28");

            json meta = ModernMeta();
            meta.erase("io.modelcontextprotocol/clientCapabilities");
            reply = Call(server,
                         {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/list"}, {"params", {{"_meta", meta}}}});
            CHECK(reply["error"]["code"] == Mcp::kInvalidParams);

            // Removed in this revision: ping and initialize.
            reply = Call(server,
                         {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "ping"}, {"params", {{"_meta", ModernMeta()}}}});
            CHECK(reply["error"]["code"] == Mcp::kMethodNotFound);
            reply = Call(server,
                         {{"jsonrpc", "2.0"},
                          {"id", 6},
                          {"method", "initialize"},
                          {"params", {{"_meta", ModernMeta()}}}});
            CHECK(reply["error"]["code"] == Mcp::kMethodNotFound);
        });

        RunTest("MCP: fingerprints compare without case for hex and with it for SSH keys", []() {
            CHECK(Mcp::SameFingerprint("ab:CD:01", "abcd01"));
            CHECK(Mcp::SameFingerprint(" SHA256:AbC+/d ", "SHA256:AbC+/d"));
            CHECK(!Mcp::SameFingerprint("SHA256:AbC+/d", "SHA256:abc+/d"));
            CHECK(!Mcp::SameFingerprint("", ""));
        });

        RunTest("MCP: tools before connecting, and a read-only tool set", []() {
            Mcp::MarconiTools tools(Mcp::ToolOptions{});
            Mcp::ToolResult result;
            CHECK(tools.CallTool("screenshot", json::object(), result));
            CHECK(result.isError);
            CHECK(tools.CallTool("status", json::object(), result));
            CHECK(!result.isError && result.content[0]["text"] == "Not connected.");
            CHECK(tools.CallTool("connect", json::object(), result)); // no address
            CHECK(result.isError);
            CHECK(!tools.CallTool("no-such-tool", json::object(), result));

            Mcp::ToolOptions readOnly;
            readOnly.readOnly = true;
            Mcp::MarconiTools lookOnly(readOnly);
            for (const Mcp::ToolDefinition& tool : lookOnly.ListTools())
            {
                CHECK(tool.readOnly);
            }
        });

        RunTest("MCP: connect and screenshot against a fake VNC server", []() {
            FakeServer server;
            Mcp::MarconiTools tools(Mcp::ToolOptions{});
            Mcp::ToolResult connectResult;
            std::thread connecting([&]() {
                tools.CallTool("connect", {{"address", "127.0.0.1::" + std::to_string(server.GetPort())}},
                               connectResult);
            });
            const bool accepted = server.AcceptClient();
            CHECK(accepted);
            if (!accepted)
            {
                connecting.join();
                return;
            }
            server.HandshakeNone("RFB 003.008\n", 4, 3, "fake desktop");
            server.ReadClientSetup();
            connecting.join();
            CHECK(!connectResult.isError);
            CHECK(connectResult.content[0]["text"].get<std::string>().find("4x3") != std::string::npos);

            // Paint the whole desktop, then take a screenshot of it and of one corner.
            Rfb::OutBuffer update;
            update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
            RectHeader(update, 0, 0, 4, 3, Rfb::Encoding::Raw);
            std::vector<uint32_t> pixels(12);
            for (int i = 0; i < 12; ++i)
            {
                pixels[i] = 0x00102030u + static_cast<uint32_t>(i);
                update.Append(&pixels[i], 4);
            }
            server.Send(update);

            Mcp::ToolResult shot;
            CHECK(tools.CallTool("screenshot", {{"settle_ms", 50}}, shot));
            CHECK(!shot.isError);
            CHECK(shot.content.size() == 2 && shot.content[0]["type"] == "image");
            CHECK(shot.content[0]["mimeType"] == "image/png");
            Mcp::Picture picture;
            CHECK(DecodePng(Base64Decode(shot.content[0]["data"].get<std::string>()), picture));
            CHECK(picture.width == 4 && picture.height == 3 && picture.pixels == pixels);
            CHECK(tools.GetView().valid && tools.GetView().width == 4);

            CHECK(tools.CallTool("screenshot", {{"area", {{"x", 2}, {"y", 1}, {"width", 10}, {"height", 10}}}}, shot));
            CHECK(!shot.isError);
            CHECK(DecodePng(Base64Decode(shot.content[0]["data"].get<std::string>()), picture));
            CHECK(picture.width == 2 && picture.height == 2); // clipped to the desktop
            CHECK(picture.pixels[0] == pixels[1 * 4 + 2]);

            Mcp::ToolResult status;
            CHECK(tools.CallTool("status", json::object(), status));
            CHECK(status.content[0]["text"].get<std::string>().find("Connected to 127.0.0.1") == 0);

            // The server goes away: the tools say so.
            server.Close();
            for (int i = 0; i < 100 && tools.GetConnection().GetFacts().connected; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(tools.CallTool("screenshot", json::object(), shot));
            CHECK(shot.isError);
        });

        RunTest("MCP: a wrong address fails with a message, not a hang", []() {
            Mcp::MarconiTools tools(Mcp::ToolOptions{});
            Mcp::ToolResult result;
            // Port 1 on this machine: nothing listens, so the connection is refused at once.
            CHECK(tools.CallTool("connect", {{"address", "127.0.0.1::1"}}, result));
            CHECK(result.isError);
            CHECK(result.content[0]["text"].get<std::string>().find("Couldn't connect") == 0);
        });

        RunTest("MCP: key names and combinations", []() {
            CHECK(Mcp::KeysymForName("Return") == Keysym::Return);
            CHECK(Mcp::KeysymForName("ENTER") == Keysym::Return);
            CHECK(Mcp::KeysymForName("page-down") == Keysym::PageDown);
            CHECK(Mcp::KeysymForName("F12") == Keysym::F1 + 11);
            CHECK(Mcp::KeysymForName("F25") == 0);
            CHECK(Mcp::KeysymForName("a") == 'a');
            CHECK(Mcp::KeysymForName("A") == 'A');
            CHECK(Mcp::KeysymForName("\xc3\xa9") == 0xe9);         // é, Latin-1
            CHECK(Mcp::KeysymForName("\xe2\x82\xac") == 0x10020ac); // €, through the Unicode range
            CHECK(Mcp::KeysymForName("cmd") == Keysym::Super_L);
            CHECK(Mcp::KeysymForName("nonsense") == 0);

            std::vector<uint32_t> held;
            uint32_t key = 0;
            std::string error;
            CHECK(Mcp::ParseKeyCombination("ctrl+alt+Delete", held, key, error));
            CHECK(held == std::vector<uint32_t>({Keysym::Control_L, Keysym::Alt_L}) && key == Keysym::Delete);
            CHECK(Mcp::ParseKeyCombination("ctrl++", held, key, error));
            CHECK(held == std::vector<uint32_t>({Keysym::Control_L}) && key == '+');
            CHECK(Mcp::ParseKeyCombination(" cmd + space ", held, key, error));
            CHECK(held == std::vector<uint32_t>({Keysym::Super_L}) && key == ' ');
            CHECK(Mcp::ParseKeyCombination("shift", held, key, error));
            CHECK(held.empty() && key == Keysym::Shift_L);
            CHECK(!Mcp::ParseKeyCombination("ctrl+banana", held, key, error));
            CHECK(error.find("banana") != std::string::npos);
            CHECK(!Mcp::ParseKeyCombination("", held, key, error));
        });

        RunTest("MCP: text becomes key presses", []() {
            std::vector<uint32_t> keysyms;
            std::string error;
            CHECK(Mcp::KeysymsForText("Hi\r\n\tx\n", keysyms, error));
            CHECK(keysyms == std::vector<uint32_t>({'H', 'i', Keysym::Return, Keysym::Tab, 'x', Keysym::Return}));
            CHECK(Mcp::KeysymsForText("\xc3\xa9\xe2\x82\xac", keysyms, error));
            CHECK(keysyms == std::vector<uint32_t>({0xe9, 0x10020ac}));
            CHECK(!Mcp::KeysymsForText("bad \xc3", keysyms, error));
            CHECK(!Mcp::KeysymsForText("bell \x07", keysyms, error));
        });

        RunTest("MCP: screenshot coordinates map back to the desktop", []() {
            Mcp::ScreenshotView view;
            int x = 0;
            int y = 0;
            std::string error;
            // No screenshot yet: desktop pixels.
            CHECK(Mcp::ToDesktop(view, 1920, 1080, 100, 200, x, y, error) && x == 100 && y == 200);
            CHECK(!Mcp::ToDesktop(view, 1920, 1080, 1920, 0, x, y, error));

            // The whole 1920x1080 desktop shown at 960x540: each picture pixel is two desktop pixels.
            view.valid = true;
            view.areaWidth = 1920;
            view.areaHeight = 1080;
            view.width = 960;
            view.height = 540;
            CHECK(Mcp::ToDesktop(view, 1920, 1080, 0, 0, x, y, error) && x == 1 && y == 1);
            CHECK(Mcp::ToDesktop(view, 1920, 1080, 959, 539, x, y, error) && x == 1919 && y == 1079);
            CHECK(!Mcp::ToDesktop(view, 1920, 1080, 960, 0, x, y, error));

            // An area at full size: offsets only.
            view.areaX = 100;
            view.areaY = 50;
            view.areaWidth = 200;
            view.areaHeight = 100;
            view.width = 200;
            view.height = 100;
            CHECK(Mcp::ToDesktop(view, 1920, 1080, 10, 20, x, y, error) && x == 110 && y == 70);
        });

        RunTest("MCP: clicks, drags and scrolls send the right pointer events", []() {
            ConnectedTools s(400, 200);
            s.Paint(400, 200);
            Mcp::ToolResult shot = s.Call("screenshot", {{"settle_ms", 20}});
            CHECK(!shot.isError);

            CHECK(!s.Call("click", {{"x", 10}, {"y", 20}}).isError);
            std::vector<Event> events = ReadEvents(s.server, 3);
            CHECK(events[0].kind == 'P' && events[0].a == 0 && events[0].b == 10 && events[0].c == 20);
            CHECK(events[1].kind == 'P' && events[1].a == 1);
            CHECK(events[2].kind == 'P' && events[2].a == 0);

            CHECK(!s.Call("click", {{"x", 5}, {"y", 5}, {"button", "right"}, {"count", 2}}).isError);
            events = ReadEvents(s.server, 5);
            CHECK(events[1].a == 4 && events[2].a == 0 && events[3].a == 4 && events[4].a == 0);

            CHECK(!s.Call("click", {{"x", 1}, {"y", 1}, {"modifiers", {"shift"}}}).isError);
            events = ReadEvents(s.server, 5);
            CHECK(events[1].kind == 'K' && events[1].a == 1 && events[1].b == Keysym::Shift_L);
            CHECK(events[2].kind == 'P' && events[2].a == 1);
            CHECK(events[4].kind == 'K' && events[4].a == 0 && events[4].b == Keysym::Shift_L);

            CHECK(!s.Call("scroll", {{"x", 50}, {"y", 60}, {"direction", "down"}, {"amount", 2}}).isError);
            events = ReadEvents(s.server, 5);
            CHECK(events[1].a == 16 && events[2].a == 0 && events[3].a == 16 && events[4].a == 0);

            CHECK(!s.Call("drag", {{"from_x", 0}, {"from_y", 0}, {"to_x", 100}, {"to_y", 50}}).isError);
            events = ReadEvents(s.server, 13); // move, press, ten steps held, release
            CHECK(events[1].a == 1 && events[1].b == 0);
            CHECK(events[11].a == 1 && events[11].b == 100 && events[11].c == 50);
            CHECK(events[12].a == 0 && events[12].b == 100);

            // Outside the screenshot: refused, nothing sent.
            Mcp::ToolResult outside = s.Call("click", {{"x", 400}, {"y", 0}});
            CHECK(outside.isError);

            // A bad button or direction says what's allowed.
            CHECK(s.Call("click", {{"x", 1}, {"y", 1}, {"button", "fourth"}}).isError);
            CHECK(s.Call("scroll", {{"x", 1}, {"y", 1}, {"direction", "sideways"}}).isError);
        });

        RunTest("MCP: typing and key combinations send the right key events", []() {
            ConnectedTools s(100, 100);
            CHECK(!s.Call("type_text", {{"text", "Ab\n"}}).isError);
            std::vector<Event> events = ReadEvents(s.server, 6);
            CHECK(events[0].kind == 'K' && events[0].a == 1 && events[0].b == 'A');
            CHECK(events[1].a == 0 && events[1].b == 'A');
            CHECK(events[2].b == 'b' && events[4].b == Keysym::Return);

            CHECK(!s.Call("key", {{"keys", "ctrl+c"}}).isError);
            events = ReadEvents(s.server, 4);
            CHECK(events[0].a == 1 && events[0].b == Keysym::Control_L);
            CHECK(events[1].a == 1 && events[1].b == 'c');
            CHECK(events[2].a == 0 && events[2].b == 'c');
            CHECK(events[3].a == 0 && events[3].b == Keysym::Control_L);

            CHECK(!s.Call("key", {{"keys", "Tab"}, {"repeat", 3}}).isError);
            events = ReadEvents(s.server, 6);
            CHECK(events[4].a == 1 && events[4].b == Keysym::Tab);

            CHECK(s.Call("key", {{"keys", "ctrl+nothing"}}).isError);
            CHECK(s.Call("type_text", {{"text", "\x07"}}).isError);
        });

        RunTest("MCP: the clipboard on a plain VNC server", []() {
            ConnectedTools s(100, 100);
            Mcp::ToolResult result = s.Call("get_clipboard");
            CHECK(!result.isError);
            CHECK(result.content[0]["text"].get<std::string>().find("hasn't shared") != std::string::npos);

            // The server's clipboard arrives (Latin-1 on the wire) and is read back as UTF-8.
            Rfb::OutBuffer cut;
            const std::string latin1 = "caf\xe9";
            cut.U8(Rfb::ServerMessage::ServerCutText).Padding(3).U32(static_cast<uint32_t>(latin1.size()));
            cut.Append(latin1.data(), latin1.size());
            s.server.Send(cut);
            for (int i = 0; i < 100; ++i)
            {
                result = s.Call("get_clipboard");
                if (result.content[0]["text"] == "caf\xc3\xa9")
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(result.content[0]["text"] == "caf\xc3\xa9");

            // Setting it sends cut text; characters outside Latin-1 are said to be lost.
            std::string sent;
            result = s.Call("set_clipboard", {{"text", "price \xe2\x82\xac" "5"}});
            CHECK(!result.isError);
            ReadEvents(s.server, 1, &sent);
            CHECK(sent == "price ?5");
            CHECK(result.content[0]["text"].get<std::string>().find("Western European") != std::string::npos);
        });

        RunTest("MCP: waiting for the screen to change", []() {
            ConnectedTools s(100, 100);
            Mcp::ToolResult result = s.Call("wait_for_change", {{"timeout_ms", 100}});
            CHECK(!result.isError && result.content[0]["text"] == "No change in 100 ms.");

            std::thread painter([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                s.Paint(100, 100);
            });
            result = s.Call("wait_for_change", {{"timeout_ms", 5000}});
            painter.join();
            CHECK(result.content[0]["text"].get<std::string>().find("The screen changed") == 0);

            // A change elsewhere doesn't count when an area is given.
            Mcp::ToolResult shot = s.Call("screenshot", {{"settle_ms", 20}});
            CHECK(!shot.isError);
            std::thread corner([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                Rfb::OutBuffer update;
                update.U8(Rfb::ServerMessage::FramebufferUpdate).Padding(1).U16(1);
                RectHeader(update, 0, 0, 1, 1, Rfb::Encoding::Raw);
                const uint32_t white = 0x00ffffffu;
                update.Append(&white, 4);
                s.server.Send(update);
            });
            result = s.Call("wait_for_change",
                            {{"timeout_ms", 300}, {"area", {{"x", 50}, {"y", 50}, {"width", 10}, {"height", 10}}}});
            corner.join();
            CHECK(result.content[0]["text"] == "No change in 300 ms.");
        });

        RunTest("MCP: a screenshot straight after input waits for the screen to answer", []() {
            ConnectedTools s(100, 100);
            s.Paint(100, 100);
            CHECK(!s.Call("screenshot", {{"settle_ms", 20}}).isError);
            std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the picture has long been still

            CHECK(!s.Call("key", {{"keys", "a"}}).isError);
            const auto start = std::chrono::steady_clock::now();
            CHECK(!s.Call("screenshot", {{"settle_ms", 200}}).isError);
            const long long waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count();
            CHECK(waited >= 180); // quiet is counted from the key press, not from the last picture
        });

        RunTest("MCP: no picture after connecting: the whole desktop is asked for again", []() {
            // Like x11vnc: the first request gets no pixels, and nothing changes on screen, so only asking again
            // brings the picture.
            ConnectedTools s(4, 2);
            Mcp::ToolResult shot;
            std::thread taking([&]() { shot = s.Call("screenshot", {{"settle_ms", 20}}); });
            const uint8_t type = s.server.In().U8();
            const uint8_t incremental = s.server.In().U8();
            s.server.In().Skip(8);
            CHECK(type == Rfb::ClientMessage::FramebufferUpdateRequest && incremental == 0);
            s.Paint(4, 2);
            taking.join();
            CHECK(!shot.isError);
            Mcp::Picture picture;
            CHECK(DecodePng(Base64Decode(shot.content[0]["data"].get<std::string>()), picture));
            CHECK(picture.width == 4 && picture.pixels[0] == 0x00808080u);
            CHECK(shot.content[1]["text"].get<std::string>().find("hasn't sent a picture") == std::string::npos);
        });

        RunTest("MCP: read-only offers no tools that change things", []() {
            Mcp::ToolOptions options;
            options.readOnly = true;
            Mcp::MarconiTools tools(options);
            std::vector<std::string> names;
            for (const Mcp::ToolDefinition& tool : tools.ListTools())
            {
                names.push_back(tool.name);
            }
            CHECK(names == std::vector<std::string>({"connect", "list_connections", "disconnect", "status",
                                                     "screenshot", "get_clipboard", "wait_for_change"}));
            Mcp::ToolResult result;
            CHECK(!tools.CallTool("click", {{"x", 1}, {"y", 1}}, result));
            CHECK(!tools.CallTool("type_text", {{"text", "x"}}, result));
        });
    }
} // namespace MarconiTest
