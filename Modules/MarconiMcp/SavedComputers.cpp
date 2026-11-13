// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The connections saved in Marconi, as MarconiMcp's tools see them

#include <MarconiMcp/SavedComputers.h>

#include <MarconiCore/Address.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

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
        } // namespace

        const SavedComputer* FindSavedComputer(const std::vector<SavedComputer>& computers, const std::string& text,
                                               std::string& error)
        {
            const std::string wanted = Lower(text);
            const SavedComputer* found = nullptr;
            int matches = 0;
            // A name first; an address only if no name matches.
            for (int pass = 0; pass < 2 && matches == 0; ++pass)
            {
                for (const SavedComputer& computer : computers)
                {
                    const std::string& candidate = pass == 0 ? computer.name : computer.address;
                    if (!candidate.empty() && Lower(candidate) == wanted)
                    {
                        found = &computer;
                        ++matches;
                    }
                }
            }
            if (matches > 1)
            {
                error = "More than one saved connection is called \"" + text + "\"; use its address instead.";
                return nullptr;
            }
            if (matches == 0)
            {
                error = "No saved connection is called \"" + text + "\". list_connections shows them.";
                return nullptr;
            }
            return found;
        }

        bool AddressFor(const SavedComputer& computer, std::string& address, std::string& error)
        {
            address = computer.address;
            Address parsed;
            std::string parseError;
            if (!ParseAddress(computer.address, parsed, parseError) || parsed.protocol != Protocol::Auto)
            {
                return true; // it names its protocol itself, or QuickConnect will say what's wrong with it
            }
            const size_t space = computer.detected.find(' ');
            if (space == std::string::npos)
            {
                return true; // never probed: a plain address is VNC
            }
            const std::string kind = computer.detected.substr(0, space);
            const long port = std::strtol(computer.detected.c_str() + space + 1, nullptr, 10);
            if (port <= 0 || port > 65535)
            {
                return true;
            }
            if (kind == "rdp")
            {
                error = "\"" + computer.DisplayName() + "\" is a Remote Desktop connection, which Marconi's AI tools "
                        "can't use yet.";
                return false;
            }
            parsed.port = static_cast<uint16_t>(port);
            parsed.protocol = kind == "marconi" ? Protocol::Marconi : Protocol::Vnc;
            address = FormatAddress(parsed);
            return true;
        }
    } // namespace Mcp
} // namespace Marconi
