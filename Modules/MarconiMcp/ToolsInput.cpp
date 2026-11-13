// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tools that act on the remote computer: the mouse, the keyboard and the clipboard, and waiting for a change

#include <MarconiMcp/Tools.h>

#include <MarconiMcp/Input.h>
#include <MarconiMcp/ToolArguments.h>

#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            void Pause(int milliseconds)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
            }

            nlohmann::json Integer(const char* description)
            {
                return {{"type", "integer"}, {"description", description}};
            }

            nlohmann::json Modifiers()
            {
                return {{"type", "array"},
                        {"items", {{"type", "string"}, {"enum", {"shift", "ctrl", "alt", "cmd", "super", "meta"}}}},
                        {"description", "Keys to hold during the action (cmd is Command on a Mac and the Windows "
                                        "key elsewhere)."}};
            }

            nlohmann::json Button()
            {
                return {{"type", "string"},
                        {"enum", {"left", "middle", "right"}},
                        {"description", "Which mouse button. Default left."}};
            }

            std::string Point(int x, int y)
            {
                return std::to_string(x) + "," + std::to_string(y);
            }

            // The longest text type_text takes in one call, so a mistake can't type for minutes.
            const size_t kMaxTypedCharacters = 5000;
        } // namespace

        void MarconiTools::AddInputTools(std::vector<ToolDefinition>& tools)
        {
            ToolDefinition click;
            click.name = "click";
            click.title = "Click";
            click.description =
                "Moves the pointer to a point in the last screenshot and clicks. count 2 double-clicks.";
            click.inputSchema = ObjectSchema({{"x", Integer("Across, in the last screenshot's pixels.")},
                                              {"y", Integer("Down, in the last screenshot's pixels.")},
                                              {"button", Button()},
                                              {"count", {{"type", "integer"},
                                                         {"minimum", 1},
                                                         {"maximum", 3},
                                                         {"description", "1 click (default), 2 double, 3 triple."}}},
                                              {"modifiers", Modifiers()}},
                                             {"x", "y"});
            tools.push_back(click);

            ToolDefinition move;
            move.name = "move";
            move.title = "Move the pointer";
            move.description = "Moves the pointer to a point in the last screenshot without clicking (for menus "
                               "and tooltips that open on hover).";
            move.inputSchema = ObjectSchema({{"x", Integer("Across, in the last screenshot's pixels.")},
                                             {"y", Integer("Down, in the last screenshot's pixels.")}},
                                            {"x", "y"});
            tools.push_back(move);

            ToolDefinition drag;
            drag.name = "drag";
            drag.title = "Drag";
            drag.description = "Presses a button at one point of the last screenshot, moves to another and lets go.";
            drag.inputSchema = ObjectSchema({{"from_x", Integer("Start, across.")},
                                             {"from_y", Integer("Start, down.")},
                                             {"to_x", Integer("End, across.")},
                                             {"to_y", Integer("End, down.")},
                                             {"button", Button()},
                                             {"modifiers", Modifiers()}},
                                            {"from_x", "from_y", "to_x", "to_y"});
            tools.push_back(drag);

            ToolDefinition scroll;
            scroll.name = "scroll";
            scroll.title = "Scroll";
            scroll.description = "Turns the mouse wheel over a point in the last screenshot.";
            scroll.inputSchema = ObjectSchema(
                {{"x", Integer("Across, in the last screenshot's pixels.")},
                 {"y", Integer("Down, in the last screenshot's pixels.")},
                 {"direction", {{"type", "string"}, {"enum", {"up", "down", "left", "right"}}}},
                 {"amount", {{"type", "integer"},
                             {"minimum", 1},
                             {"maximum", 50},
                             {"description", "Wheel clicks. Default 3."}}},
                 {"modifiers", Modifiers()}},
                {"x", "y", "direction"});
            tools.push_back(scroll);

            ToolDefinition typeText;
            typeText.name = "type_text";
            typeText.title = "Type text";
            typeText.description = "Types text on the remote computer's keyboard, into whatever has the focus. "
                                   "New lines press Return and tabs press Tab. For shortcuts and special keys use "
                                   "the key tool.";
            typeText.inputSchema = ObjectSchema({{"text", Property("string", "What to type (up to 5000 characters).")}},
                                                {"text"});
            tools.push_back(typeText);

            ToolDefinition key;
            key.name = "key";
            key.title = "Press keys";
            key.description = "Presses a key or a combination, such as \"Return\", \"Escape\", \"ctrl+c\", "
                              "\"cmd+space\", \"alt+F4\", \"ctrl+alt+Delete\", \"shift+Tab\", \"page_down\", "
                              "\"F5\". cmd is Command on a Mac and the Windows key elsewhere.";
            key.inputSchema = ObjectSchema({{"keys", Property("string", "The key or combination, joined with +.")},
                                            {"repeat", {{"type", "integer"},
                                                        {"minimum", 1},
                                                        {"maximum", 100},
                                                        {"description", "How many times. Default 1."}}}},
                                           {"keys"});
            tools.push_back(key);

            ToolDefinition getClipboard;
            getClipboard.name = "get_clipboard";
            getClipboard.title = "Read the clipboard";
            getClipboard.description = "The remote computer's clipboard text. Some computers only share it when it "
                                       "changes, so copy something there first.";
            getClipboard.inputSchema = ObjectSchema(nlohmann::json::object());
            getClipboard.readOnly = true;
            tools.push_back(getClipboard);

            ToolDefinition setClipboard;
            setClipboard.name = "set_clipboard";
            setClipboard.title = "Set the clipboard";
            setClipboard.description = "Puts text on the remote computer's clipboard, ready to paste there.";
            setClipboard.inputSchema = ObjectSchema({{"text", Property("string", "The text.")}}, {"text"});
            tools.push_back(setClipboard);

            ToolDefinition wait;
            wait.name = "wait_for_change";
            wait.title = "Wait for the screen to change";
            wait.description = "Waits until the screen changes (anywhere, or in an area of the last screenshot), or "
                               "until the timeout. Useful after starting something slow.";
            wait.inputSchema = ObjectSchema(
                {{"timeout_ms", {{"type", "integer"},
                                 {"minimum", 0},
                                 {"maximum", 60000},
                                 {"description", "How long to wait at most. Default 10000."}}},
                 {"area", {{"type", "object"},
                           {"description", "Only changes here count, in the last screenshot's pixels."},
                           {"properties", {{"x", {{"type", "integer"}}},
                                           {"y", {{"type", "integer"}}},
                                           {"width", {{"type", "integer"}}},
                                           {"height", {{"type", "integer"}}}}},
                           {"required", {"x", "y", "width", "height"}},
                           {"additionalProperties", false}}}});
            wait.readOnly = true;
            tools.push_back(wait);
        }

        bool MarconiTools::CallInputTool(const std::string& name, const nlohmann::json& arguments, ToolResult& out)
        {
            if (name == "click")
            {
                out = Click(arguments);
            }
            else if (name == "move")
            {
                out = Move(arguments);
            }
            else if (name == "drag")
            {
                out = Drag(arguments);
            }
            else if (name == "scroll")
            {
                out = Scroll(arguments);
            }
            else if (name == "type_text")
            {
                out = TypeText(arguments);
            }
            else if (name == "key")
            {
                out = Key(arguments);
            }
            else if (name == "get_clipboard")
            {
                out = GetClipboard();
            }
            else if (name == "set_clipboard")
            {
                out = SetClipboard(arguments);
            }
            else if (name == "wait_for_change")
            {
                out = WaitForChange(arguments);
            }
            else
            {
                return false;
            }
            return true;
        }

        RemoteSession& MarconiTools::RequireSession()
        {
            RemoteSession* session = m_connection.GetSession();
            if (!session)
            {
                const ConnectionFacts facts = m_connection.GetFacts();
                throw ArgumentError{facts.lastError.empty()
                                        ? "Not connected. Call connect first."
                                        : "Not connected: the connection ended (" + facts.lastError + ")."};
            }
            return *session;
        }

        void MarconiTools::PointArgument(const nlohmann::json& arguments, const char* xKey, const char* yKey, int& x,
                                         int& y)
        {
            if (!arguments.contains(xKey) || !arguments.contains(yKey))
            {
                throw ArgumentError{std::string(xKey) + " and " + yKey + " are needed."};
            }
            const int pictureX = GetInt(arguments, xKey, 0, -(1 << 20), 1 << 20);
            const int pictureY = GetInt(arguments, yKey, 0, -(1 << 20), 1 << 20);
            const ConnectionFacts facts = m_connection.GetFacts();
            std::string error;
            if (!ToDesktop(m_view, facts.width, facts.height, pictureX, pictureY, x, y, error))
            {
                throw ArgumentError{error};
            }
        }

        void MarconiTools::PointerTo(RemoteSession& session, int x, int y, uint8_t buttons)
        {
            session.SendPointerEvent(x, y, buttons);
            m_connection.SetPointer(x, y);
        }

        std::vector<uint32_t> MarconiTools::ModifiersArgument(const nlohmann::json& arguments)
        {
            std::vector<uint32_t> keys;
            if (!arguments.contains("modifiers") || arguments["modifiers"].is_null())
            {
                return keys;
            }
            const nlohmann::json& modifiers = arguments["modifiers"];
            if (!modifiers.is_array())
            {
                throw ArgumentError{"modifiers must be a list, such as [\"shift\"]."};
            }
            for (const nlohmann::json& name : modifiers)
            {
                const uint32_t keysym = name.is_string() ? KeysymForName(name.get<std::string>()) : 0;
                if (keysym == 0 || !IsModifier(keysym))
                {
                    throw ArgumentError{"modifiers can be shift, ctrl, alt, cmd, super or meta."};
                }
                keys.push_back(keysym);
            }
            return keys;
        }

        namespace
        {
            // Holds modifier keys for as long as it lives.
            class HeldKeys
            {
            public:
                HeldKeys(RemoteSession& session, std::vector<uint32_t> keys)
                    : m_session(session), m_keys(std::move(keys))
                {
                    for (uint32_t key : m_keys)
                    {
                        m_session.SendKeyEvent(key, true);
                    }
                }
                ~HeldKeys()
                {
                    for (auto it = m_keys.rbegin(); it != m_keys.rend(); ++it)
                    {
                        m_session.SendKeyEvent(*it, false);
                    }
                }
                HeldKeys(const HeldKeys&) = delete;
                HeldKeys& operator=(const HeldKeys&) = delete;

            private:
                RemoteSession& m_session;
                std::vector<uint32_t> m_keys;
            };

            uint8_t ButtonArgument(const nlohmann::json& arguments)
            {
                const std::string name = GetString(arguments, "button", false);
                if (name.empty())
                {
                    return 1;
                }
                const uint8_t bit = ButtonBit(name);
                if (bit == 0)
                {
                    throw ArgumentError{"button must be left, middle or right."};
                }
                return bit;
            }
        } // namespace

        ToolResult MarconiTools::Click(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            int x = 0;
            int y = 0;
            PointArgument(arguments, "x", "y", x, y);
            const uint8_t button = ButtonArgument(arguments);
            const int count = GetInt(arguments, "count", 1, 1, 3);
            const std::vector<uint32_t> modifiers = ModifiersArgument(arguments);

            PointerTo(session, x, y, 0);
            {
                HeldKeys held(session, modifiers);
                for (int i = 0; i < count; ++i)
                {
                    if (i > 0)
                    {
                        Pause(kInputPauseMs);
                    }
                    PointerTo(session, x, y, button);
                    PointerTo(session, x, y, 0);
                }
            }
            m_connection.NoteInput();
            ToolResult result;
            const char* what = count == 1 ? "Clicked" : (count == 2 ? "Double-clicked" : "Triple-clicked");
            result.AddText(std::string(what) + " at desktop " + Point(x, y) + ". Take a screenshot to see the result.");
            return result;
        }

        ToolResult MarconiTools::Move(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            int x = 0;
            int y = 0;
            PointArgument(arguments, "x", "y", x, y);
            PointerTo(session, x, y, 0);
            m_connection.NoteInput();
            ToolResult result;
            result.AddText("Moved the pointer to desktop " + Point(x, y) + ".");
            return result;
        }

        ToolResult MarconiTools::Drag(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            int fromX = 0;
            int fromY = 0;
            int toX = 0;
            int toY = 0;
            PointArgument(arguments, "from_x", "from_y", fromX, fromY);
            PointArgument(arguments, "to_x", "to_y", toX, toY);
            const uint8_t button = ButtonArgument(arguments);
            const std::vector<uint32_t> modifiers = ModifiersArgument(arguments);

            PointerTo(session, fromX, fromY, 0);
            {
                HeldKeys held(session, modifiers);
                PointerTo(session, fromX, fromY, button);
                // In steps, so the other computer sees a drag rather than a jump.
                const int kSteps = 10;
                for (int step = 1; step <= kSteps; ++step)
                {
                    Pause(kInputPauseMs);
                    PointerTo(session, fromX + (toX - fromX) * step / kSteps, fromY + (toY - fromY) * step / kSteps,
                              button);
                }
                Pause(kInputPauseMs);
                PointerTo(session, toX, toY, 0);
            }
            m_connection.NoteInput();
            ToolResult result;
            result.AddText("Dragged from desktop " + Point(fromX, fromY) + " to " + Point(toX, toY) + ".");
            return result;
        }

        ToolResult MarconiTools::Scroll(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            int x = 0;
            int y = 0;
            PointArgument(arguments, "x", "y", x, y);
            const std::string direction = GetString(arguments, "direction", true);
            const uint8_t wheel = WheelBit(direction);
            if (wheel == 0)
            {
                throw ArgumentError{"direction must be up, down, left or right."};
            }
            const int amount = GetInt(arguments, "amount", 3, 1, 50);
            const std::vector<uint32_t> modifiers = ModifiersArgument(arguments);

            PointerTo(session, x, y, 0);
            {
                HeldKeys held(session, modifiers);
                for (int i = 0; i < amount; ++i)
                {
                    // One wheel click is the button pressed and released.
                    PointerTo(session, x, y, wheel);
                    PointerTo(session, x, y, 0);
                }
            }
            m_connection.NoteInput();
            ToolResult result;
            result.AddText("Scrolled " + direction + " " + std::to_string(amount) + " at desktop " + Point(x, y) + ".");
            return result;
        }

        ToolResult MarconiTools::TypeText(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            const std::string text = GetString(arguments, "text", true);
            std::vector<uint32_t> keysyms;
            std::string error;
            if (!KeysymsForText(text, keysyms, error))
            {
                throw ArgumentError{error};
            }
            if (keysyms.size() > kMaxTypedCharacters)
            {
                throw ArgumentError{"That's more than 5000 characters; type it in parts, or use set_clipboard and "
                                    "paste."};
            }
            for (uint32_t keysym : keysyms)
            {
                session.SendKeyEvent(keysym, true);
                session.SendKeyEvent(keysym, false);
            }
            m_connection.NoteInput();
            ToolResult result;
            result.AddText("Typed " + std::to_string(keysyms.size()) + " characters.");
            return result;
        }

        ToolResult MarconiTools::Key(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            const std::string keys = GetString(arguments, "keys", true);
            const int repeat = GetInt(arguments, "repeat", 1, 1, 100);
            std::vector<uint32_t> held;
            uint32_t key = 0;
            std::string error;
            if (!ParseKeyCombination(keys, held, key, error))
            {
                throw ArgumentError{error};
            }
            for (int i = 0; i < repeat; ++i)
            {
                HeldKeys holding(session, held);
                session.SendKeyEvent(key, true);
                session.SendKeyEvent(key, false);
            }
            m_connection.NoteInput();
            ToolResult result;
            result.AddText("Pressed " + keys + (repeat > 1 ? " " + std::to_string(repeat) + " times." : "."));
            return result;
        }

        ToolResult MarconiTools::GetClipboard()
        {
            RequireSession();
            bool received = false;
            const std::string text = m_connection.GetServerClipboard(std::chrono::milliseconds(2000), received);
            ToolResult result;
            if (!received)
            {
                result.AddText("The remote computer hasn't shared its clipboard. Many only do when something is "
                               "copied there, so copy it (for example with the key tool and ctrl+c, or cmd+c on a "
                               "Mac) and ask again.");
            }
            else if (text.empty())
            {
                result.AddText("The remote clipboard holds no text.");
            }
            else
            {
                result.AddText(text);
            }
            return result;
        }

        ToolResult MarconiTools::SetClipboard(const nlohmann::json& arguments)
        {
            RemoteSession& session = RequireSession();
            const std::string text = GetString(arguments, "text", true);
            Rfb::ClipboardContent content;
            content.utf8Text = ToNetworkLineEndings(text);
            ToolResult result;
            if (session.OfferClipboard(content))
            {
                result.AddText("Put " + std::to_string(text.size()) + " bytes of text on the remote clipboard.");
                return result;
            }
            // Plain VNC carries only Western European characters.
            const std::string latin1 = Utf8ToLatin1(content.utf8Text);
            session.SendClientCutText(latin1);
            std::string message = "Put the text on the remote clipboard.";
            if (Latin1ToUtf8(latin1) != content.utf8Text)
            {
                message += " This computer only takes Western European characters this way, so others became "
                           "\"?\"; type_text can type them instead.";
            }
            result.AddText(message);
            return result;
        }

        ToolResult MarconiTools::WaitForChange(const nlohmann::json& arguments)
        {
            RequireSession();
            const int timeoutMs = GetInt(arguments, "timeout_ms", 10000, 0, 60000);
            Rfb::Rect area;
            const Rfb::Rect* areaPointer = nullptr;
            if (arguments.contains("area") && !arguments["area"].is_null())
            {
                const nlohmann::json& box = arguments["area"];
                if (!box.is_object())
                {
                    throw ArgumentError{"area must be an object with x, y, width and height."};
                }
                int x0 = 0;
                int y0 = 0;
                int x1 = 0;
                int y1 = 0;
                const int width = GetInt(box, "width", 1, 1, 1 << 16);
                const int height = GetInt(box, "height", 1, 1, 1 << 16);
                PointArgument(box, "x", "y", x0, y0);
                // The far corner, clipped to the screenshot.
                nlohmann::json corner = {{"x", GetInt(box, "x", 0, 0, 1 << 16) + width - 1},
                                         {"y", GetInt(box, "y", 0, 0, 1 << 16) + height - 1}};
                if (m_view.valid)
                {
                    corner["x"] = std::min(corner["x"].get<int>(), m_view.width - 1);
                    corner["y"] = std::min(corner["y"].get<int>(), m_view.height - 1);
                }
                PointArgument(corner, "x", "y", x1, y1);
                area.x = x0;
                area.y = y0;
                area.width = std::max(1, x1 - x0 + 1);
                area.height = std::max(1, y1 - y0 + 1);
                areaPointer = &area;
            }
            const auto start = std::chrono::steady_clock::now();
            const bool changed = m_connection.WaitForUpdate(std::chrono::milliseconds(timeoutMs), areaPointer);
            const long long waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count();
            if (!m_connection.GetFacts().connected)
            {
                return ToolResult::Error("The connection ended while waiting.");
            }
            ToolResult result;
            result.AddText(changed ? "The screen changed after " + std::to_string(waited) + " ms."
                                   : "No change in " + std::to_string(timeoutMs) + " ms.");
            return result;
        }
    } // namespace Mcp
} // namespace Marconi
