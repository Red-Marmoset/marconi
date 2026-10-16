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
    } // namespace Files
} // namespace Marconi
