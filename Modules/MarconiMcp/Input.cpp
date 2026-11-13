// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning what an AI asks for into input: key names and combinations, typed text, mouse buttons, and screenshot
// coordinates back into desktop ones

#include <MarconiMcp/Input.h>

#include <MarconiMcp/Tools.h>

#include <MarconiCore/Keysyms.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            std::string Lower(std::string text)
            {
                std::transform(text.begin(), text.end(), text.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                return text;
            }

            // Decodes one UTF-8 character at `at`; 0 if malformed. Moves `at` past it.
            uint32_t NextCodepoint(const std::string& text, size_t& at)
            {
                const unsigned char first = static_cast<unsigned char>(text[at]);
                int length = 0;
                uint32_t codepoint = 0;
                if (first < 0x80)
                {
                    ++at;
                    return first;
                }
                else if ((first & 0xe0) == 0xc0)
                {
                    length = 2;
                    codepoint = first & 0x1f;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    length = 3;
                    codepoint = first & 0x0f;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    length = 4;
                    codepoint = first & 0x07;
                }
                else
                {
                    return 0;
                }
                if (at + length > text.size())
                {
                    return 0;
                }
                for (int i = 1; i < length; ++i)
                {
                    const unsigned char next = static_cast<unsigned char>(text[at + i]);
                    if ((next & 0xc0) != 0x80)
                    {
                        return 0;
                    }
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                at += length;
                return codepoint;
            }

            struct NamedKey
            {
                const char* name;
                uint32_t keysym;
            };

            const NamedKey kNamedKeys[] = {
                {"return", Keysym::Return},
                {"enter", Keysym::Return},
                {"tab", Keysym::Tab},
                {"escape", Keysym::Escape},
                {"esc", Keysym::Escape},
                {"backspace", Keysym::BackSpace},
                {"delete", Keysym::Delete},
                {"del", Keysym::Delete},
                {"insert", Keysym::Insert},
                {"space", ' '},
                {"home", Keysym::Home},
                {"end", Keysym::End},
                {"page_up", Keysym::PageUp},
                {"pageup", Keysym::PageUp},
                {"page_down", Keysym::PageDown},
                {"pagedown", Keysym::PageDown},
                {"left", Keysym::Left},
                {"right", Keysym::Right},
                {"up", Keysym::Up},
                {"down", Keysym::Down},
                {"print", Keysym::Print},
                {"printscreen", Keysym::Print},
                {"pause", Keysym::Pause},
                {"menu", Keysym::Menu},
                {"capslock", Keysym::CapsLock},
                {"caps_lock", Keysym::CapsLock},
                {"numlock", Keysym::NumLock},
                {"scrolllock", Keysym::ScrollLock},
                {"plus", '+'},
                {"minus", '-'},
                {"shift", Keysym::Shift_L},
                {"ctrl", Keysym::Control_L},
                {"control", Keysym::Control_L},
                {"alt", Keysym::Alt_L},
                {"option", Keysym::Alt_L},
                {"altgr", Keysym::ISO_Level3_Shift},
                // Command on a Mac and the Windows key elsewhere, as the viewer sends them.
                {"cmd", Keysym::Super_L},
                {"command", Keysym::Super_L},
                {"super", Keysym::Super_L},
                {"win", Keysym::Super_L},
                {"windows", Keysym::Super_L},
                {"meta", Keysym::Meta_L},
            };
        } // namespace

        uint32_t KeysymForName(const std::string& name)
        {
            if (name.empty())
            {
                return 0;
            }
            size_t at = 0;
            const uint32_t single = NextCodepoint(name, at);
            if (at == name.size() && single != 0)
            {
                return Keysym::FromUnicode(single); // one character, case kept: "a", "A", "é"
            }
            std::string lower = Lower(name);
            std::replace(lower.begin(), lower.end(), '-', '_');
            for (const NamedKey& key : kNamedKeys)
            {
                if (lower == key.name)
                {
                    return key.keysym;
                }
            }
            if (lower.size() >= 2 && lower[0] == 'f' &&
                std::all_of(lower.begin() + 1, lower.end(),
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
            {
                const int number = std::atoi(lower.c_str() + 1);
                if (number >= 1 && number <= 24)
                {
                    return Keysym::F1 + static_cast<uint32_t>(number - 1);
                }
            }
            return 0;
        }

        bool IsModifier(uint32_t keysym)
        {
            return (keysym >= Keysym::Shift_L && keysym <= Keysym::Super_R) || keysym == Keysym::ISO_Level3_Shift;
        }

        bool ParseKeyCombination(const std::string& text, std::vector<uint32_t>& held, uint32_t& key,
                                 std::string& error)
        {
            held.clear();
            key = 0;
            // Split on '+', ignoring spaces around the names. A '+' with no name before it is the + key itself
            // ("ctrl++"), and a lone space is the space bar.
            std::vector<std::string> parts;
            std::string current;
            const auto finish = [&parts, &current]() {
                while (current.size() > 1 && current.back() == ' ')
                {
                    current.pop_back();
                }
                if (!current.empty())
                {
                    parts.push_back(current);
                }
                current.clear();
            };
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '+' && !current.empty())
                {
                    finish();
                }
                else if (text[i] != ' ' || !current.empty() || text.find_first_not_of(' ') == std::string::npos)
                {
                    current += text[i];
                }
            }
            finish();
            if (parts.empty())
            {
                error = "keys is empty.";
                return false;
            }
            for (size_t i = 0; i < parts.size(); ++i)
            {
                const uint32_t keysym = KeysymForName(parts[i]);
                if (keysym == 0)
                {
                    error = "Unknown key \"" + parts[i] + "\". Use names like ctrl, shift, alt, cmd, Return, Tab, "
                            "Escape, BackSpace, Delete, F1-F24, left, page_down, space, or a single character.";
                    return false;
                }
                if (i + 1 < parts.size())
                {
                    held.push_back(keysym);
                }
                else
                {
                    key = keysym;
                }
            }
            return true;
        }

        bool KeysymsForText(const std::string& utf8, std::vector<uint32_t>& keysyms, std::string& error)
        {
            keysyms.clear();
            size_t at = 0;
            while (at < utf8.size())
            {
                const uint32_t codepoint = NextCodepoint(utf8, at);
                if (codepoint == 0)
                {
                    error = "The text isn't valid UTF-8.";
                    return false;
                }
                if (codepoint == '\r')
                {
                    if (at < utf8.size() && utf8[at] == '\n')
                    {
                        continue; // "\r\n" is one Return, sent for the "\n"
                    }
                    keysyms.push_back(Keysym::Return);
                }
                else if (codepoint == '\n')
                {
                    keysyms.push_back(Keysym::Return);
                }
                else if (codepoint == '\t')
                {
                    keysyms.push_back(Keysym::Tab);
                }
                else
                {
                    const uint32_t keysym = Keysym::FromUnicode(codepoint);
                    if (keysym == 0)
                    {
                        error = "The text holds a control character that can't be typed; use the key tool for "
                                "special keys.";
                        return false;
                    }
                    keysyms.push_back(keysym);
                }
            }
            return true;
        }

        uint8_t ButtonBit(const std::string& name)
        {
            const std::string lower = Lower(name);
            if (lower == "left")
            {
                return 1;
            }
            if (lower == "middle")
            {
                return 2;
            }
            if (lower == "right")
            {
                return 4;
            }
            return 0;
        }

        uint8_t WheelBit(const std::string& direction)
        {
            // Buttons 4 and 5 are the wheel up and down, 6 and 7 left and right, as the viewer sends them.
            const std::string lower = Lower(direction);
            if (lower == "up")
            {
                return 8;
            }
            if (lower == "down")
            {
                return 16;
            }
            if (lower == "left")
            {
                return 32;
            }
            if (lower == "right")
            {
                return 64;
            }
            return 0;
        }

        bool ToDesktop(const ScreenshotView& view, int desktopWidth, int desktopHeight, int x, int y, int& outX,
                       int& outY, std::string& error)
        {
            if (desktopWidth <= 0 || desktopHeight <= 0)
            {
                error = "The computer hasn't sent a picture yet.";
                return false;
            }
            if (!view.valid)
            {
                if (x < 0 || y < 0 || x >= desktopWidth || y >= desktopHeight)
                {
                    error = "That point is outside the desktop (" + std::to_string(desktopWidth) + "x" +
                            std::to_string(desktopHeight) + "). Take a screenshot first to use its coordinates.";
                    return false;
                }
                outX = x;
                outY = y;
                return true;
            }
            if (x < 0 || y < 0 || x >= view.width || y >= view.height)
            {
                error = "That point is outside the last screenshot (" + std::to_string(view.width) + "x" +
                        std::to_string(view.height) + ").";
                return false;
            }
            // The middle of the desktop pixels this picture pixel stands for.
            const long long desktopX = view.areaX + (2LL * x + 1) * view.areaWidth / (2LL * view.width);
            const long long desktopY = view.areaY + (2LL * y + 1) * view.areaHeight / (2LL * view.height);
            outX = static_cast<int>(std::min<long long>(std::max<long long>(desktopX, 0), desktopWidth - 1));
            outY = static_cast<int>(std::min<long long>(std::max<long long>(desktopY, 0), desktopHeight - 1));
            return true;
        }
    } // namespace Mcp
} // namespace Marconi
