// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tools an AI tool can call: connecting, screenshots, and (later) the mouse, keyboard and clipboard

#include <MarconiMcp/Tools.h>

#include <MarconiMcp/Base64.h>
#include <MarconiMcp/Picture.h>
#include <MarconiMcp/Png.h>
#include <MarconiMcp/ToolArguments.h>

#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            std::string DescribeScreens(const ConnectionFacts& facts)
            {
                std::ostringstream text;
                text << "Desktop " << facts.width << "x" << facts.height;
                if (!facts.desktopName.empty())
                {
                    text << " (\"" << facts.desktopName << "\")";
                }
                text << ".";
                if (facts.screens.size() > 1)
                {
                    text << " " << facts.screens.size() << " displays:";
                    for (const Rfb::Screen& screen : facts.screens)
                    {
                        text << " " << screen.width << "x" << screen.height << " at " << screen.x << "," << screen.y
                             << ";";
                    }
                }
                text << (facts.encrypted ? " Encrypted (" + facts.encryptionName + ")." : " Not encrypted.");
                return text.str();
            }
        } // namespace

        std::string ServerInstructions()
        {
            return "Marconi connects to another computer's desktop (VNC, or Marconi's own server) so you can see it "
                   "and use it. Call connect first, then screenshot to see the screen. Coordinates for the mouse "
                   "tools are in the pixels of the most recent screenshot. Take a new screenshot after acting to "
                   "check the result. One connection at a time.";
        }

        MarconiTools::MarconiTools(ToolOptions options) : m_options(std::move(options)) {}

        std::vector<ToolDefinition> MarconiTools::ListTools() const
        {
            std::vector<ToolDefinition> tools;

            ToolDefinition connect;
            connect.name = "connect";
            connect.title = "Connect to a computer";
            connect.description =
                "Connects to a computer's desktop, ending any earlier connection. Give either a connection saved in "
                "Marconi, by its name (list_connections shows them; its saved password, protocol and settings are "
                "used), or an address: \"host\", \"host:display\", \"host::port\", \"vnc://host\", "
                "\"marconi://host\" (Marconi's own server) or \"ssh://user@host\" (VNC through the computer's SSH "
                "server). If the computer's certificate or SSH key isn't trusted, the result gives its fingerprint; "
                "connect again with trust_fingerprint set to it to accept it.";
            connect.inputSchema = ObjectSchema(
                {{"connection", Property("string", "The name of a connection saved in Marconi, such as \"studio\", "
                                                    "or of a computer list_connections found on this network.")},
                 {"address", Property("string", "The computer's address, when it isn't a saved connection.")},
                 {"username", Property("string", "User name, when the computer asks for an account.")},
                 {"password", Property("string", "Password. If left out, the MARCONI_PASSWORD environment "
                                                 "variable is used, when set.")},
                 {"trust_fingerprint", Property("string", "Accept a certificate or SSH key with this fingerprint "
                                                          "(from an earlier connect result).")},
                 {"quality", {{"type", "string"},
                              {"enum", {"best", "balanced", "low"}},
                              {"description", "Picture quality: best (no loss), balanced (default) or low "
                                              "(for slow links)."}}}});
            connect.readOnly = true;
            tools.push_back(connect);

            ToolDefinition list;
            list.name = "list_connections";
            list.title = "Saved connections";
            list.description = "The computers saved in Marconi (name, address, group and notes, and whether a "
                               "password is saved; passwords themselves are never shown), and the ones found on this "
                               "network that aren't saved. Connect to either kind by its name. Use this when the "
                               "user names a computer.";
            list.inputSchema = ObjectSchema(nlohmann::json::object());
            list.readOnly = true;
            tools.push_back(list);

            ToolDefinition disconnect;
            disconnect.name = "disconnect";
            disconnect.title = "Disconnect";
            disconnect.description = "Ends the connection.";
            disconnect.inputSchema = ObjectSchema(nlohmann::json::object());
            disconnect.readOnly = true;
            tools.push_back(disconnect);

            ToolDefinition status;
            status.name = "status";
            status.title = "Connection status";
            status.description = "Whether connected, to what, the desktop's size and displays, and whether the "
                                 "connection is encrypted.";
            status.inputSchema = ObjectSchema(nlohmann::json::object());
            status.readOnly = true;
            tools.push_back(status);

            ToolDefinition screenshot;
            screenshot.name = "screenshot";
            screenshot.title = "Take a screenshot";
            screenshot.description =
                "Shows the remote desktop, or part of it, as a picture. Big desktops are scaled down to fit "
                "max_size; ask for an area to see detail at full size. Waits for the screen to stop changing "
                "first. Later mouse coordinates are in this picture's pixels.";
            screenshot.inputSchema = ObjectSchema(
                {{"area", {{"type", "object"},
                           {"description", "Part of the desktop to show, in desktop pixels (see status)."},
                           {"properties", {{"x", {{"type", "integer"}}},
                                           {"y", {{"type", "integer"}}},
                                           {"width", {{"type", "integer"}}},
                                           {"height", {{"type", "integer"}}}}},
                           {"required", {"x", "y", "width", "height"}},
                           {"additionalProperties", false}}},
                 {"max_size", {{"type", "integer"},
                               {"minimum", 64},
                               {"maximum", kLargestMaxSide},
                               {"description", "Longest side of the picture in pixels. Default 1568."}}},
                 {"settle_ms", {{"type", "integer"},
                                {"minimum", 0},
                                {"maximum", 5000},
                                {"description", "How long the screen must be still before the picture is "
                                                "taken. Default 300. A screen that keeps changing is taken "
                                                "anyway after 2 seconds (or twice this)."}}}});
            screenshot.readOnly = true;
            tools.push_back(screenshot);

            AddInputTools(tools);

            if (m_options.readOnly)
            {
                tools.erase(std::remove_if(tools.begin(), tools.end(),
                                           [](const ToolDefinition& tool) { return !tool.readOnly; }),
                            tools.end());
            }
            return tools;
        }

        bool MarconiTools::CallTool(const std::string& name, const nlohmann::json& arguments, ToolResult& out)
        {
            const std::vector<ToolDefinition> tools = ListTools();
            const auto named = [&name](const ToolDefinition& tool) { return tool.name == name; };
            if (std::none_of(tools.begin(), tools.end(), named))
            {
                return false;
            }
            try
            {
                if (name == "connect")
                {
                    out = Connect(arguments);
                }
                else if (name == "list_connections")
                {
                    out = ListConnections();
                }
                else if (name == "disconnect")
                {
                    out = Disconnect();
                }
                else if (name == "status")
                {
                    out = Status();
                }
                else if (name == "screenshot")
                {
                    out = Screenshot(arguments);
                }
                else if (!CallInputTool(name, arguments, out))
                {
                    return false;
                }
            }
            catch (const ArgumentError& error)
            {
                out = ToolResult::Error(error.message);
            }
            return true;
        }

        ToolResult MarconiTools::Connect(const nlohmann::json& arguments)
        {
            ConnectRequest request;
            const std::string name = GetString(arguments, "connection", false);
            request.address = GetString(arguments, "address", false);
            if (name.empty() && request.address.empty())
            {
                throw ArgumentError{"connection (a saved connection's name) or address is needed."};
            }

            // A saved connection: by its name, or the one whose address this is. Its settings, and its password
            // (fetched here, never shown to the AI), are used unless the call gives its own.
            std::string savedPassword;
            std::string savedName;
            if (m_options.saved)
            {
                const std::vector<SavedComputer> computers = m_options.saved->List();
                std::string error;
                const SavedComputer* computer =
                    FindSavedComputer(computers, name.empty() ? request.address : name, error);
                if (!computer && !name.empty() && !FindNetworkComputer(name, request.address, savedName))
                {
                    throw ArgumentError{error};
                }
                if (computer)
                {
                    if (!AddressFor(*computer, request.address, error))
                    {
                        throw ArgumentError{error};
                    }
                    savedName = computer->DisplayName();
                    request.username = computer->username;
                    request.sshTunnel = computer->sshTunnel;
                    request.trustedCertificate = HexToBytes(computer->certificate);
                    request.trustedSshHostKey = computer->sshHostKey;
                    request.quality = QualityFromName(computer->quality);
                    if (computer->hasPassword)
                    {
                        m_options.saved->Password(computer->id, savedPassword);
                    }
                }
            }
            else if (!name.empty() && !FindNetworkComputer(name, request.address, savedName))
            {
                throw ArgumentError{"Marconi's saved connections can't be read here; give an address instead."};
            }
            const std::string username = GetString(arguments, "username", false);
            if (!username.empty())
            {
                request.username = username;
            }
            request.password = GetString(arguments, "password", false);
            if (request.password.empty())
            {
                request.password = savedPassword.empty() ? m_options.defaultPassword : savedPassword;
            }
            request.trustFingerprint = GetString(arguments, "trust_fingerprint", false);
            const std::string quality = GetString(arguments, "quality", false);
            if (!quality.empty())
            {
                if (quality != "best" && quality != "balanced" && quality != "low")
                {
                    throw ArgumentError{"quality must be best, balanced or low."};
                }
                request.quality = QualityFromName(quality);
            }

            m_view = ScreenshotView();
            LOG_INFO("Mcp", "Connecting to %s", request.address.c_str());
            const ConnectOutcome outcome = m_connection.Connect(request);
            if (!outcome.connected)
            {
                std::string message = "Couldn't connect to " + request.address + ": " + outcome.error;
                if (!outcome.untrustedFingerprint.empty())
                {
                    message += " Fingerprint: " + outcome.untrustedFingerprint +
                               ". Only if the user expects this computer, connect again with trust_fingerprint "
                               "set to it.";
                }
                else if (outcome.needsPassword)
                {
                    message += " Ask the user for the password (or to set MARCONI_PASSWORD), then connect again.";
                }
                LOG_INFO("Mcp", "Connecting to %s failed: %s", request.address.c_str(), outcome.error.c_str());
                return ToolResult::Error(message);
            }
            ToolResult result;
            const std::string where = savedName.empty() ? request.address : savedName + " (" + request.address + ")";
            result.AddText("Connected to " + where + ". " + DescribeScreens(m_connection.GetFacts()) +
                           " Take a screenshot to see it.");
            return result;
        }

        ToolResult MarconiTools::ListConnections()
        {
            if (!m_options.saved && !m_options.discover)
            {
                return ToolResult::Error("Marconi's saved connections can't be read here.");
            }
            const std::vector<SavedComputer> computers =
                m_options.saved ? m_options.saved->List() : std::vector<SavedComputer>();
            nlohmann::json list = nlohmann::json::array();
            for (const SavedComputer& computer : computers)
            {
                nlohmann::json entry = {{"name", computer.DisplayName()}, {"address", computer.address}};
                if (!computer.group.empty())
                {
                    entry["group"] = computer.group;
                }
                if (!computer.notes.empty())
                {
                    entry["notes"] = computer.notes;
                }
                if (!computer.username.empty())
                {
                    entry["user"] = computer.username;
                }
                if (computer.sshTunnel)
                {
                    entry["through_ssh"] = true;
                }
                if (computer.favourite)
                {
                    entry["favourite"] = true;
                }
                entry["password_saved"] = computer.hasPassword;
                list.push_back(entry);
            }
            const nlohmann::json network = NetworkList(computers);
            ToolResult result;
            if (list.empty() && network.empty())
            {
                result.AddText("No connections are saved in Marconi, and no computers were found on this network.");
                return result;
            }
            nlohmann::json all = {{"saved", list}};
            if (m_options.discover)
            {
                all["on_this_network"] = network;
            }
            result.AddText(all.dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
            return result;
        }

        ToolResult MarconiTools::Disconnect()
        {
            const bool wasConnected = m_connection.GetFacts().connected;
            m_connection.Disconnect();
            m_view = ScreenshotView();
            ToolResult result;
            result.AddText(wasConnected ? "Disconnected." : "Wasn't connected.");
            return result;
        }

        ToolResult MarconiTools::Status()
        {
            const ConnectionFacts facts = m_connection.GetFacts();
            ToolResult result;
            if (!facts.connected)
            {
                std::string text = "Not connected.";
                if (!facts.lastError.empty())
                {
                    text += " The last connection, to " + facts.address + ", ended: " + facts.lastError;
                }
                result.AddText(text);
                return result;
            }
            std::string text = "Connected to " + facts.address + ". " + DescribeScreens(facts);
            if (m_view.valid)
            {
                text += " The last screenshot showed " + std::to_string(m_view.areaWidth) + "x" +
                        std::to_string(m_view.areaHeight) + " at " + std::to_string(m_view.areaX) + "," +
                        std::to_string(m_view.areaY) + " as " + std::to_string(m_view.width) + "x" +
                        std::to_string(m_view.height) + ".";
            }
            result.AddText(text);
            return result;
        }

        ToolResult MarconiTools::Screenshot(const nlohmann::json& arguments)
        {
            RemoteSession* session = m_connection.GetSession();
            if (!session)
            {
                const ConnectionFacts facts = m_connection.GetFacts();
                return ToolResult::Error(facts.lastError.empty()
                                             ? "Not connected. Call connect first."
                                             : "Not connected: the connection ended (" + facts.lastError + ").");
            }
            const int maxSide = GetInt(arguments, "max_size", kDefaultMaxSide, 64, kLargestMaxSide);
            const int settleMs = GetInt(arguments, "settle_ms", 300, 0, 5000);
            const long long maxPixels = maxSide > kDefaultMaxSide ? kLargestMaxPixels : kDefaultMaxPixels;

            const bool havePicture = m_connection.EnsurePicture(std::chrono::milliseconds(3000));
            // Ask for the whole desktop every time: some servers stop reporting changes, and a stale picture
            // would mislead the AI about what its last action did.
            if (havePicture)
            {
                m_connection.RefreshPicture(std::chrono::milliseconds(1500));
            }
            // A screen that never stops changing (a clock, a video) is taken anyway after a while.
            const bool settled = m_connection.WaitUntilQuiet(std::chrono::milliseconds(settleMs),
                                                             std::chrono::milliseconds(std::max(2000, settleMs * 2)));

            Picture picture;
            int areaX = 0;
            int areaY = 0;
            int areaWidth = 0;
            int areaHeight = 0;
            {
                Rfb::Framebuffer& framebuffer = session->GetFramebuffer();
                std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
                const int width = framebuffer.GetWidth();
                const int height = framebuffer.GetHeight();
                if (width <= 0 || height <= 0)
                {
                    return ToolResult::Error("The computer hasn't sent a picture yet. Try again in a moment.");
                }
                areaWidth = width;
                areaHeight = height;
                if (arguments.contains("area") && !arguments["area"].is_null())
                {
                    const nlohmann::json& area = arguments["area"];
                    if (!area.is_object())
                    {
                        throw ArgumentError{"area must be an object with x, y, width and height."};
                    }
                    areaX = GetInt(area, "x", -1, 0, width - 1);
                    areaY = GetInt(area, "y", -1, 0, height - 1);
                    areaWidth = GetInt(area, "width", -1, 1, 1 << 16);
                    areaHeight = GetInt(area, "height", -1, 1, 1 << 16);
                    if (areaX < 0 || areaY < 0 || areaWidth < 1 || areaHeight < 1)
                    {
                        throw ArgumentError{"area needs x, y, width and height."};
                    }
                    // Clip to the desktop rather than refuse.
                    areaWidth = std::min(areaWidth, width - areaX);
                    areaHeight = std::min(areaHeight, height - areaY);
                }
                picture = CopyArea(framebuffer.Row(0), static_cast<size_t>(width), areaX, areaY, areaWidth,
                                   areaHeight);
            }

            int pointerX = 0;
            int pointerY = 0;
            if (m_connection.GetPointer(pointerX, pointerY))
            {
                DrawCursor(picture, m_connection.GetCursor(), pointerX - areaX, pointerY - areaY);
            }

            const PictureSize size = FitWithin(areaWidth, areaHeight, maxSide, maxPixels);
            picture = ScaleDown(picture, size.width, size.height);
            const std::vector<uint8_t> png = EncodePng(picture.pixels.data(), picture.width, picture.height,
                                                       static_cast<size_t>(picture.width));
            if (png.empty())
            {
                return ToolResult::Error("Couldn't make the picture.");
            }

            m_view.valid = true;
            m_view.areaX = areaX;
            m_view.areaY = areaY;
            m_view.areaWidth = areaWidth;
            m_view.areaHeight = areaHeight;
            m_view.width = picture.width;
            m_view.height = picture.height;

            ToolResult result;
            result.AddImage(Base64Encode(png), "image/png");
            std::ostringstream text;
            text << "Showing desktop area " << areaWidth << "x" << areaHeight << " at " << areaX << "," << areaY
                 << " as a " << picture.width << "x" << picture.height << " picture";
            if (picture.width != areaWidth)
            {
                char scale[32];
                std::snprintf(scale, sizeof(scale), "%.3f", static_cast<double>(areaWidth) / picture.width);
                text << " (each picture pixel is " << scale << " desktop pixels)";
            }
            text << ". Mouse coordinates are in this picture's pixels.";
            if (!havePicture)
            {
                text << " The computer hasn't sent a picture yet, so this may be blank; try again in a moment.";
            }
            else if (!settled)
            {
                text << " The screen was still changing when this was taken.";
            }
            result.AddText(text.str());
            return result;
        }
    } // namespace Mcp
} // namespace Marconi
