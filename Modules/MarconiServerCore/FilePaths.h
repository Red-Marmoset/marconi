// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading the paths a viewer sends for file transfer: one clean form, and a refusal for anything odd

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        enum class PathStyle
        {
            Posix,  // "/home/sem/file": one tree
            Windows // "C:/Users/sem/file": a tree per drive, and "/" lists the drives
        };

        PathStyle NativePathStyle();

        constexpr size_t kMaxPathBytes = 4096;
        constexpr size_t kMaxNameBytes = 255;

        struct PathRules
        {
            PathStyle style = NativePathStyle();
            // Windows style only: paths start with a drive ("C:/..." or "/C:/..."), and "/" alone is the list of
            // drives. Off when the paths are inside one shared folder, where "/" is that folder.
            bool drives = true;
        };

        struct ParsedPath
        {
            std::string drive;              // "C:" (upper case) or empty
            std::vector<std::string> parts; // the folders and the name, none of them "", "." or ".."
        };

        // Turns what the viewer sent into a clean path. "." and empty parts are dropped, ".." removes the part
        // before it, and a ".." with nothing before it is refused (it would climb out of the top), as are:
        // an empty or relative path; one over kMaxPathBytes; a part over kMaxNameBytes; text that isn't UTF-8;
        // control characters; backslashes (the wire uses '/'); and on Windows style the characters Windows
        // forbids, names ending in a dot or space, device names (CON, NUL, COM1...), and "C:" anywhere but first.
        bool ParsePath(const std::string& wire, const PathRules& rules, ParsedPath& outPath, std::string& error);

        // The clean path as text: "/a/b" (Posix, or Windows without drives), "C:/a/b" and "C:/" for a drive, and
        // "/" for the list of drives.
        std::string FormatPath(const ParsedPath& path, const PathRules& rules);

        // True if one name (a part of a path) is usable under these rules.
        bool IsValidName(const std::string& name, PathStyle style, std::string& error);
    } // namespace Server
} // namespace Marconi
