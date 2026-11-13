// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Helpers for showing files: the clickable parts of a path, sorting a folder's contents, and type names

#include <MarconiCore/Files/FileView.h>

#include <algorithm>
#include <cctype>

namespace Marconi
{
    namespace Files
    {
        namespace
        {
            std::string Lower(const std::string& text)
            {
                std::string lower = text;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return lower;
            }

            int CompareNames(const std::string& a, const std::string& b)
            {
                return Lower(a).compare(Lower(b));
            }
        } // namespace

        std::vector<PathPart> SplitPath(const std::string& path)
        {
            std::vector<PathPart> parts;
            std::string sofar;
            size_t start = 0;
            if (!path.empty() && path[0] == '/')
            {
                parts.push_back({"/", "/"});
                sofar = "/";
                start = 1;
            }
            while (start < path.size())
            {
                size_t slash = path.find('/', start);
                std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                start = slash == std::string::npos ? path.size() : slash + 1;
                if (name.empty())
                {
                    continue;
                }
                if (sofar.empty() && name.size() == 2 && name[1] == ':')
                {
                    sofar = name + "/"; // a Windows drive: "C:" leads to "C:/"
                    parts.push_back({name, sofar});
                    continue;
                }
                sofar = JoinPath(sofar, name);
                parts.push_back({name, sofar});
            }
            return parts;
        }

        void SortEntries(std::vector<FileEntry>& entries, SortColumn column, bool ascending)
        {
            std::stable_sort(entries.begin(), entries.end(), [column, ascending](const FileEntry& a, const FileEntry& b) {
                if (a.isFolder != b.isFolder)
                {
                    return a.isFolder;
                }
                int order = 0;
                switch (column)
                {
                    case SortColumn::Name: order = CompareNames(a.name, b.name); break;
                    case SortColumn::Size: order = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0); break;
                    case SortColumn::Modified:
                        order = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
                        break;
                    case SortColumn::Type: order = CompareNames(TypeName(a), TypeName(b)); break;
                }
                if (order != 0)
                {
                    return ascending ? order < 0 : order > 0;
                }
                return CompareNames(a.name, b.name) < 0; // ties go by name
            });
        }

        std::string TypeName(const FileEntry& entry)
        {
            if (entry.isFolder)
            {
                return "Folder";
            }
            size_t dot = entry.name.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 == entry.name.size())
            {
                return "File";
            }
            std::string extension = entry.name.substr(dot + 1);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return extension + " file";
        }

        namespace
        {
            bool IsSeparator(char c)
            {
                return c == '/' || c == '\\';
            }

            // Where the drive letter sits in `path` if it starts with one ("C:" or "/C:"), else npos.
            size_t DriveLetterAt(const std::string& path)
            {
                size_t at = (!path.empty() && IsSeparator(path[0])) ? 1 : 0;
                if (path.size() >= at + 2 && std::isalpha(static_cast<unsigned char>(path[at])) && path[at + 1] == ':' &&
                    (path.size() == at + 2 || IsSeparator(path[at + 2])))
                {
                    return at;
                }
                return std::string::npos;
            }
        } // namespace

        std::string ToDisplayPath(const std::string& path)
        {
            // Inside Marconi a Windows path always starts with a slash; only those are changed.
            if (path.empty() || path[0] != '/' || DriveLetterAt(path) != 1)
            {
                return path;
            }
            std::string shown = path.substr(1);
            std::replace(shown.begin(), shown.end(), '/', '\\');
            if (shown.size() == 2)
            {
                shown += '\\'; // "C:" is a drive; "C:\" is how it is written
            }
            return shown;
        }

        std::string FromTypedPath(const std::string& typed)
        {
            size_t first = typed.find_first_not_of(" \t");
            if (first == std::string::npos)
            {
                return std::string();
            }
            size_t last = typed.find_last_not_of(" \t");
            std::string text = typed.substr(first, last - first + 1);
            size_t at = DriveLetterAt(text);
            if (at == std::string::npos)
            {
                return text;
            }
            std::string path = "/" + text.substr(at);
            std::replace(path.begin(), path.end(), '\\', '/');
            if (path.size() == 3)
            {
                path += '/';
            }
            return path;
        }
    } // namespace Files
} // namespace Marconi
