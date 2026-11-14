// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Path helpers shared by every file source

#include <MarconiCore/Files/FileSource.h>

namespace Marconi
{
    namespace Files
    {
        namespace
        {
            // "/" or "C:/": the end of the line when going up.
            bool IsRoot(const std::string& path)
            {
                return path == "/" || (path.size() == 3 && path[1] == ':' && path[2] == '/');
            }

            std::string WithoutTrailingSlash(const std::string& path)
            {
                std::string trimmed = path;
                while (trimmed.size() > 1 && trimmed.back() == '/' && !IsRoot(trimmed))
                {
                    trimmed.pop_back();
                }
                return trimmed;
            }
        } // namespace

        std::string JoinPath(const std::string& folder, const std::string& name)
        {
            if (folder.empty())
            {
                return name;
            }
            return folder.back() == '/' ? folder + name : folder + "/" + name;
        }

        std::string ParentPath(const std::string& path)
        {
            std::string trimmed = WithoutTrailingSlash(path);
            if (IsRoot(trimmed))
            {
                return trimmed;
            }
            size_t slash = trimmed.find_last_of('/');
            if (slash == std::string::npos)
            {
                return trimmed;
            }
            std::string parent = trimmed.substr(0, slash + 1);
            return IsRoot(parent) ? parent : WithoutTrailingSlash(parent);
        }

        std::string FileName(const std::string& path)
        {
            std::string trimmed = WithoutTrailingSlash(path);
            size_t slash = trimmed.find_last_of('/');
            return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
        }

        bool IsSafeEntryName(const std::string& name)
        {
            if (name.empty() || name == "." || name == "..")
            {
                return false;
            }
            for (char c : name)
            {
                if (c == '/' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                {
                    return false;
                }
            }
#if defined(_WIN32)
            // Windows also splits paths at a backslash, reads "name:stream" and "C:name" specially, and drops
            // dots and spaces from the end of a name (so ".. " would become "..").
            if (name.find_first_of("\\:") != std::string::npos || name.back() == '.' || name.back() == ' ')
            {
                return false;
            }
            // Device names (CON, NUL, COM1...) open the device whatever folder and extension they are given.
            std::string stem = name.substr(0, name.find('.'));
            while (!stem.empty() && stem.back() == ' ')
            {
                stem.pop_back();
            }
            for (char& c : stem)
            {
                c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
            }
            if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
                stem == "CONOUT$")
            {
                return false;
            }
            if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
                stem[3] >= '0' && stem[3] <= '9')
            {
                return false;
            }
#endif
            return true;
        }

        std::string KeepBothName(const std::string& name, const std::function<bool(const std::string&)>& taken)
        {
            // The number goes before the last extension; a name that starts with its only dot has no extension.
            size_t dot = name.find_last_of('.');
            std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
            std::string extension = dot == std::string::npos || dot == 0 ? "" : name.substr(dot);
            for (int number = 2;; ++number)
            {
                std::string candidate = stem + " (" + std::to_string(number) + ")" + extension;
                if (!taken(candidate))
                {
                    return candidate;
                }
            }
        }
    } // namespace Files
} // namespace Marconi
