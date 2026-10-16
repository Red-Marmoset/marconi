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
    } // namespace Files
} // namespace Marconi
