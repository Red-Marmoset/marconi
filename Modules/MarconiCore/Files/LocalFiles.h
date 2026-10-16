// Copyright (c) 2026 Red Marmoset Studios Ltd.
// This computer's files, for the file panel's left side

#pragma once

#include <MarconiCore/Files/FileSource.h>

namespace Marconi
{
    namespace Files
    {
        class LocalFiles : public FileSource
        {
        public:
            std::string GetHome() override;
            bool List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error) override;
            bool Stat(const std::string& path, FileEntry& outEntry, std::string& error) override;
            bool MakeFolder(const std::string& path, std::string& error) override;
            bool Rename(const std::string& from, const std::string& to, std::string& error) override;
            bool Remove(const std::string& path, std::string& error) override;
            bool RemoveFolder(const std::string& path, std::string& error) override;
            std::unique_ptr<FileReader> OpenRead(const std::string& path, std::string& error) override;
            std::unique_ptr<FileWriter> OpenWrite(const std::string& path, bool overwrite,
                                                  std::string& error) override;
        };
    } // namespace Files
} // namespace Marconi
