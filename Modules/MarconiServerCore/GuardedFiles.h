// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server's own files as a viewer may see them: every path is cleaned first, and may be held inside one folder
//
// The Windows-only parts (listing the drives, Windows paths) have not been built or run on Windows yet.

#pragma once

#include "FilePaths.h"

#include <MarconiCore/Files/LocalFiles.h>
#include <MarconiCore/Wire/WireFileMessages.h>

#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // Wraps Files::LocalFiles. With no root, a viewer can reach the whole computer the way the person at it can
        // (symbolic links are followed as they would be there). With a root, the root is "/" to the viewer and
        // nothing outside it can be named: ".." can't climb out, and a symbolic link pointing outside is refused.
        // Errors say what went wrong without showing the root's real place on disk.
        // Safe to use from one thread at a time.
        class GuardedFiles
        {
        public:
            // `root` is a folder on this computer, or empty for no limit.
            GuardedFiles(PathStyle style, const std::string& root);

            // False (with `error`) when a root was given and can't be used; every call then fails.
            bool IsUsable(std::string& error) const;
            bool IsConfined() const { return !m_root.empty(); }

            // Cleans `wire` and finds where it is on disk. `isRoot` says it names the top (the root, or a drive's
            // top, or the list of drives).
            Wire::FileStatus Resolve(const std::string& wire, std::string& outReal, std::string& error,
                                     bool* isRoot = nullptr);

            Wire::FileStatus Home(std::string& outPath, std::string& error);
            // Where dropped files go: the Desktop folder of the user the server runs as, else their home.
            Wire::FileStatus Desktop(std::string& outPath, std::string& error);
            Wire::FileStatus List(const std::string& folder, std::vector<Wire::FileInfo>& outEntries,
                                  std::string& error);
            Wire::FileStatus Stat(const std::string& path, Wire::FileInfo& outEntry, std::string& error);
            Wire::FileStatus MakeFolder(const std::string& path, std::string& error);
            Wire::FileStatus Rename(const std::string& from, const std::string& to, std::string& error);
            Wire::FileStatus Remove(const std::string& path, std::string& error);
            Wire::FileStatus RemoveFolder(const std::string& path, std::string& error);

        private:
            bool Inside(const std::string& real, std::string& error) const;
            std::string Hide(const std::string& message) const; // drops the root's place from an error

            PathRules m_rules;
            std::string m_root;          // as given, with '/' and no trailing '/'
            std::string m_rootCanonical; // the same with links resolved
            std::string m_rootError;
            Files::LocalFiles m_local;
        };
    } // namespace Server
} // namespace Marconi
