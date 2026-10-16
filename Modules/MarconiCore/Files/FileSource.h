// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the file panel and the transfer queue need from a computer's files: this one, or a remote one

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Files
    {
        struct FileEntry
        {
            std::string name; // without the folder
            uint64_t size = 0;
            int64_t modified = 0; // seconds since 1970 (UTC); 0 if unknown
            bool isFolder = false;
        };

        class FileReader
        {
        public:
            virtual ~FileReader() = default;
            // Bytes read, 0 at the end, -1 on error.
            virtual ptrdiff_t Read(void* buffer, size_t size, std::string& error) = 0;
        };

        class FileWriter
        {
        public:
            virtual ~FileWriter() = default;
            virtual bool Write(const void* data, size_t size, std::string& error) = 0;
            // Closes the file. A writer destroyed without Finish leaves whatever was written.
            virtual bool Finish(std::string& error) = 0;
        };

        // Paths use '/' everywhere (Windows paths look like "C:/Users/sem"). Implementations are safe to use from
        // the file panel and the transfer queue at the same time.
        class FileSource
        {
        public:
            virtual ~FileSource() = default;

            // Where to start browsing: the user's home folder.
            virtual std::string GetHome() = 0;
            virtual bool List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error) = 0;
            virtual bool Stat(const std::string& path, FileEntry& outEntry, std::string& error) = 0;
            virtual bool MakeFolder(const std::string& path, std::string& error) = 0;
            virtual bool Rename(const std::string& from, const std::string& to, std::string& error) = 0;
            virtual bool Remove(const std::string& path, std::string& error) = 0;
            virtual bool RemoveFolder(const std::string& path, std::string& error) = 0;
            virtual std::unique_ptr<FileReader> OpenRead(const std::string& path, std::string& error) = 0;
            // With `overwrite` false, fails if the file exists.
            virtual std::unique_ptr<FileWriter> OpenWrite(const std::string& path, bool overwrite,
                                                          std::string& error) = 0;
        };

        std::string JoinPath(const std::string& folder, const std::string& name);
        // The folder holding `path`; a root ("/" or "C:/") is its own parent.
        std::string ParentPath(const std::string& path);
        std::string FileName(const std::string& path);
        // "report.pdf" -> "report (2).pdf", or (3) and so on: the first name `taken` says is free.
        std::string KeepBothName(const std::string& name, const std::function<bool(const std::string&)>& taken);
    } // namespace Files
} // namespace Marconi
