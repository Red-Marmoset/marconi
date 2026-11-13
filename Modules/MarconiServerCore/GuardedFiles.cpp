// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server's own files as a viewer may see them

#include "GuardedFiles.h"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h> // GetLogicalDrives, for listing the drives (not built on Windows yet)
#endif

namespace Marconi
{
    namespace Server
    {
        namespace fs = std::filesystem;
        using Wire::FileStatus;

        namespace
        {
            fs::path ToPath(const std::string& text)
            {
#if defined(__cpp_char8_t)
                return fs::path(std::u8string(text.begin(), text.end()));
#else
                return fs::u8path(text);
#endif
            }

            std::string ToText(const fs::path& path)
            {
                auto text = path.generic_u8string();
                return std::string(text.begin(), text.end());
            }

            bool IsDriveList(const ParsedPath& path, const PathRules& rules)
            {
                return rules.style == PathStyle::Windows && rules.drives && path.drive.empty();
            }
        } // namespace

        GuardedFiles::GuardedFiles(PathStyle style, const std::string& root)
        {
            m_rules.style = style;
            m_rules.drives = root.empty(); // inside a root, "/" is the root, not a list of drives
            if (root.empty())
            {
                return;
            }
            m_root = root;
            for (char& c : m_root)
            {
                if (c == '\\')
                {
                    c = '/';
                }
            }
            while (m_root.size() > 1 && m_root.back() == '/')
            {
                m_root.pop_back();
            }
            std::error_code code;
            fs::path canonical = fs::canonical(ToPath(m_root), code);
            if (code || !fs::is_directory(canonical, code))
            {
                m_rootError = "The shared folder " + m_root + " can't be used";
                return;
            }
            m_rootCanonical = ToText(canonical);
        }

        bool GuardedFiles::IsUsable(std::string& error) const
        {
            if (!m_rootError.empty())
            {
                error = m_rootError;
                return false;
            }
            return true;
        }

        std::string GuardedFiles::Hide(const std::string& message) const
        {
            std::string text = message;
            for (const std::string* root : {&m_rootCanonical, &m_root})
            {
                if (root->empty() || *root == "/")
                {
                    continue;
                }
                for (size_t at = text.find(*root); at != std::string::npos; at = text.find(*root, at))
                {
                    text.erase(at, root->size());
                }
            }
            return text;
        }

        bool GuardedFiles::Inside(const std::string& real, std::string& error) const
        {
            std::error_code code;
            fs::path resolved = fs::weakly_canonical(ToPath(real), code);
            if (code)
            {
                error = "That path can't be used";
                return false;
            }
            fs::path root = ToPath(m_rootCanonical);
            auto rootIt = root.begin();
            auto it = resolved.begin();
            for (; rootIt != root.end(); ++rootIt, ++it)
            {
                if (it == resolved.end() || *it != *rootIt)
                {
                    error = "That path is outside the shared folder";
                    return false;
                }
            }
            return true;
        }

        FileStatus GuardedFiles::Resolve(const std::string& wire, std::string& outReal, std::string& error,
                                         bool* isRoot)
        {
            if (!IsUsable(error))
            {
                return FileStatus::Failed;
            }
            ParsedPath parsed;
            if (!ParsePath(wire, m_rules, parsed, error))
            {
                return FileStatus::BadPath;
            }
            if (isRoot != nullptr)
            {
                *isRoot = parsed.parts.empty();
            }
            const std::string clean = FormatPath(parsed, m_rules);
            if (!IsConfined())
            {
                outReal = clean;
                return FileStatus::Ok;
            }
            outReal = clean == "/" ? m_root : m_root + clean;
            if (!Inside(outReal, error))
            {
                return FileStatus::BadPath;
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::Home(std::string& outPath, std::string& error)
        {
            if (!IsUsable(error))
            {
                return FileStatus::Failed;
            }
            outPath = "/";
            if (IsConfined())
            {
                return FileStatus::Ok;
            }
            ParsedPath parsed;
            std::string ignored;
            if (ParsePath(m_local.GetHome(), m_rules, parsed, ignored))
            {
                outPath = FormatPath(parsed, m_rules);
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::Desktop(std::string& outPath, std::string& error)
        {
            FileStatus status = Home(outPath, error);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            std::string desktop = Files::JoinPath(outPath, "Desktop");
            std::string real;
            std::error_code code;
            if (Resolve(desktop, real, error) == FileStatus::Ok && fs::is_directory(ToPath(real), code))
            {
                outPath = desktop;
            }
            error.clear();
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::List(const std::string& folder, std::vector<Wire::FileInfo>& outEntries,
                                      std::string& error)
        {
            outEntries.clear();
            std::string real;
            ParsedPath parsed;
            std::string parseError;
            if (!ParsePath(folder, m_rules, parsed, parseError))
            {
                error = parseError;
                return FileStatus::BadPath;
            }
#if defined(_WIN32)
            if (IsDriveList(parsed, m_rules))
            {
                DWORD mask = GetLogicalDrives();
                for (int i = 0; i < 26; ++i)
                {
                    if ((mask & (1u << i)) != 0)
                    {
                        Wire::FileInfo drive;
                        drive.name = std::string(1, static_cast<char>('A' + i)) + ":";
                        drive.isFolder = true;
                        outEntries.push_back(drive);
                    }
                }
                return FileStatus::Ok;
            }
#else
            if (IsDriveList(parsed, m_rules))
            {
                error = "There are no drives to list on this computer";
                return FileStatus::BadPath;
            }
#endif
            FileStatus status = Resolve(folder, real, error);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            std::vector<Files::FileEntry> entries;
            if (!m_local.List(real, entries, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            for (const Files::FileEntry& entry : entries)
            {
                std::string nameError;
                if (!IsValidName(entry.name, m_rules.style, nameError))
                {
                    continue; // a name the viewer couldn't use anyway
                }
                if (IsConfined())
                {
                    // A link pointing out of the shared folder isn't shown.
                    std::error_code code;
                    std::string child = Files::JoinPath(real, entry.name);
                    if (fs::is_symlink(fs::symlink_status(ToPath(child), code)) && !Inside(child, nameError))
                    {
                        continue;
                    }
                }
                if (outEntries.size() >= Wire::kMaxListEntries)
                {
                    outEntries.clear();
                    error = "That folder has too many items to list";
                    return FileStatus::Failed;
                }
                Wire::FileInfo info;
                info.name = entry.name;
                info.size = entry.size;
                info.modified = entry.modified;
                info.isFolder = entry.isFolder;
                outEntries.push_back(std::move(info));
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::Stat(const std::string& path, Wire::FileInfo& outEntry, std::string& error)
        {
            ParsedPath parsed;
            std::string parseError;
            if (!ParsePath(path, m_rules, parsed, parseError))
            {
                error = parseError;
                return FileStatus::BadPath;
            }
            if (IsDriveList(parsed, m_rules))
            {
                outEntry = Wire::FileInfo();
                outEntry.isFolder = true;
                return FileStatus::Ok;
            }
            std::string real;
            FileStatus status = Resolve(path, real, error);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            Files::FileEntry entry;
            if (!m_local.Stat(real, entry, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            outEntry.name = parsed.parts.empty() ? std::string() : parsed.parts.back();
            outEntry.size = entry.size;
            outEntry.modified = entry.modified;
            outEntry.isFolder = entry.isFolder;
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::MakeFolder(const std::string& path, std::string& error)
        {
            std::string real;
            bool isRoot = false;
            FileStatus status = Resolve(path, real, error, &isRoot);
            if (status == FileStatus::Ok && isRoot)
            {
                error = "That folder already exists";
                return FileStatus::Failed;
            }
            if (status != FileStatus::Ok)
            {
                return status;
            }
            if (!m_local.MakeFolder(real, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::Rename(const std::string& from, const std::string& to, std::string& error)
        {
            std::string realFrom;
            std::string realTo;
            bool fromRoot = false;
            bool toRoot = false;
            FileStatus status = Resolve(from, realFrom, error, &fromRoot);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            status = Resolve(to, realTo, error, &toRoot);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            if (fromRoot || toRoot)
            {
                error = "A top folder can't be renamed";
                return FileStatus::BadPath;
            }
            if (!m_local.Rename(realFrom, realTo, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::Remove(const std::string& path, std::string& error)
        {
            std::string real;
            bool isRoot = false;
            FileStatus status = Resolve(path, real, error, &isRoot);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            if (isRoot)
            {
                error = "A top folder can't be deleted";
                return FileStatus::BadPath;
            }
            if (!m_local.Remove(real, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            return FileStatus::Ok;
        }

        FileStatus GuardedFiles::RemoveFolder(const std::string& path, std::string& error)
        {
            std::string real;
            bool isRoot = false;
            FileStatus status = Resolve(path, real, error, &isRoot);
            if (status != FileStatus::Ok)
            {
                return status;
            }
            if (isRoot)
            {
                error = "A top folder can't be deleted";
                return FileStatus::BadPath;
            }
            if (!m_local.RemoveFolder(real, error))
            {
                error = Hide(error);
                return FileStatus::Failed;
            }
            return FileStatus::Ok;
        }
    } // namespace Server
} // namespace Marconi
