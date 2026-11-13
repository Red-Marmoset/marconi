// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Helpers for showing files: the clickable parts of a path, sorting a folder's contents, and type names

#pragma once

#include <MarconiCore/Files/FileSource.h>

#include <string>
#include <vector>

namespace Marconi
{
    namespace Files
    {
        struct PathPart
        {
            std::string label; // what to show, e.g. "Users"
            std::string path;  // where clicking it goes, e.g. "C:/Users"
        };

        // "C:/Users/Sem" -> C: (C:/), Users (C:/Users), Sem (C:/Users/Sem). A '/' root is its own first part.
        std::vector<PathPart> SplitPath(const std::string& path);

        enum class SortColumn
        {
            Name,
            Size,
            Modified,
            Type,
        };

        // Folders always come first; ties go by name. Names compare ignoring case.
        void SortEntries(std::vector<FileEntry>& entries, SortColumn column, bool ascending);

        // "Folder", "PDF file", or "File" when there's no extension.
        std::string TypeName(const FileEntry& entry);

        // How a path is shown to people. A Windows path, which is kept as "/C:/Users/Sam/Desktop" inside Marconi, shows
        // as "C:\Users\Sam\Desktop"; any other path is shown as it is.
        std::string ToDisplayPath(const std::string& path);
        // The reverse, for a path someone typed: "C:\Users\Sam", "C:/Users/Sam" and "/C:/Users/Sam" all become
        // "/C:/Users/Sam" (a drive letter alone, "C:", becomes "/C:/"). A path that doesn't start with a drive
        // letter is returned as typed, apart from trimmed spaces.
        std::string FromTypedPath(const std::string& typed);
    } // namespace Files
} // namespace Marconi
