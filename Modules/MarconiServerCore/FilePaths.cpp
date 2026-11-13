// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading the paths a viewer sends for file transfer

#include "FilePaths.h"

#include <MarconiCore/Wire/WireFileMessages.h>

#include <cstring>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            bool IsDriveToken(const std::string& token)
            {
                return token.size() == 2 && token[1] == ':' &&
                       ((token[0] >= 'A' && token[0] <= 'Z') || (token[0] >= 'a' && token[0] <= 'z'));
            }

            bool IsDeviceName(const std::string& name)
            {
                // The part before the first dot, without trailing spaces, in upper case.
                std::string base = name.substr(0, name.find('.'));
                while (!base.empty() && base.back() == ' ')
                {
                    base.pop_back();
                }
                for (char& c : base)
                {
                    if (c >= 'a' && c <= 'z')
                    {
                        c = static_cast<char>(c - 'a' + 'A');
                    }
                }
                if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || base == "CONIN$" ||
                    base == "CONOUT$")
                {
                    return true;
                }
                return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) &&
                       base[3] >= '1' && base[3] <= '9';
            }
        } // namespace

        PathStyle NativePathStyle()
        {
#if defined(_WIN32)
            return PathStyle::Windows;
#else
            return PathStyle::Posix;
#endif
        }

        bool IsValidName(const std::string& name, PathStyle style, std::string& error)
        {
            if (name.empty() || name == "." || name == "..")
            {
                error = "That isn't a usable file name";
                return false;
            }
            if (name.size() > kMaxNameBytes)
            {
                error = "A name in that path is too long";
                return false;
            }
            for (unsigned char c : name)
            {
                if (c < 0x20 || c == 0x7F)
                {
                    error = "A name in that path has a control character";
                    return false;
                }
                if (c == '\\' || c == '/')
                {
                    error = "A name in that path has a slash in it";
                    return false;
                }
                if (style == PathStyle::Windows && std::strchr("<>:\"|?*", c) != nullptr)
                {
                    error = std::string("Windows doesn't allow ") + static_cast<char>(c) + " in a name";
                    return false;
                }
            }
            if (style == PathStyle::Windows)
            {
                if (name.back() == '.' || name.back() == ' ')
                {
                    error = "Windows doesn't allow a name ending in a dot or a space";
                    return false;
                }
                if (IsDeviceName(name))
                {
                    error = "That name is reserved by Windows";
                    return false;
                }
            }
            return true;
        }

        bool ParsePath(const std::string& wire, const PathRules& rules, ParsedPath& outPath, std::string& error)
        {
            outPath = ParsedPath();
            if (wire.empty())
            {
                error = "The path is empty";
                return false;
            }
            if (wire.size() > kMaxPathBytes)
            {
                error = "The path is too long";
                return false;
            }
            for (unsigned char c : wire)
            {
                if (c < 0x20 || c == 0x7F)
                {
                    error = "The path has a control character";
                    return false;
                }
                if (c == '\\')
                {
                    error = "The path has a backslash; use / between folders";
                    return false;
                }
            }
            if (!Wire::IsValidUtf8(wire))
            {
                error = "The path isn't valid text";
                return false;
            }

            std::vector<std::string> tokens;
            size_t start = 0;
            while (start <= wire.size())
            {
                size_t slash = wire.find('/', start);
                if (slash == std::string::npos)
                {
                    slash = wire.size();
                }
                tokens.push_back(wire.substr(start, slash - start));
                start = slash + 1;
            }

            const bool windowsDrives = rules.style == PathStyle::Windows && rules.drives;
            size_t index = 0;
            if (windowsDrives)
            {
                // "C:/x" or "/C:/x" (the empty first token is the leading slash).
                size_t first = 0;
                while (first < tokens.size() && tokens[first].empty())
                {
                    ++first;
                }
                if (first < tokens.size() && IsDriveToken(tokens[first]))
                {
                    char letter = tokens[first][0];
                    if (letter >= 'a' && letter <= 'z')
                    {
                        letter = static_cast<char>(letter - 'a' + 'A');
                    }
                    outPath.drive = std::string(1, letter) + ":";
                    index = first + 1;
                }
                else if (wire[0] != '/')
                {
                    error = "The path must start with a drive such as C:/";
                    return false;
                }
                else
                {
                    index = 0; // "/": the list of drives; anything after it must be empty
                }
            }
            else if (wire[0] != '/')
            {
                error = "The path must start with /";
                return false;
            }

            for (; index < tokens.size(); ++index)
            {
                const std::string& token = tokens[index];
                if (token.empty() || token == ".")
                {
                    continue;
                }
                if (token == "..")
                {
                    if (outPath.parts.empty())
                    {
                        error = "The path goes above the top folder";
                        return false;
                    }
                    outPath.parts.pop_back();
                    continue;
                }
                if (windowsDrives && outPath.drive.empty())
                {
                    error = "Choose a drive first, such as C:/";
                    return false;
                }
                if (!IsValidName(token, rules.style, error))
                {
                    return false;
                }
                outPath.parts.push_back(token);
            }
            return true;
        }

        std::string FormatPath(const ParsedPath& path, const PathRules& rules)
        {
            std::string text;
            if (rules.style == PathStyle::Windows && rules.drives)
            {
                if (path.drive.empty())
                {
                    return "/";
                }
                text = path.drive;
                if (path.parts.empty())
                {
                    return text + "/";
                }
            }
            else if (path.parts.empty())
            {
                return "/";
            }
            for (const std::string& part : path.parts)
            {
                text += "/" + part;
            }
            return text;
        }
    } // namespace Server
} // namespace Marconi
