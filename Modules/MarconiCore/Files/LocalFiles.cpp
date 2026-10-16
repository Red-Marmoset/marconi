// Copyright (c) 2026 Red Marmoset Studios Ltd.
// This computer's files, for the file panel's left side

#include <MarconiCore/Files/LocalFiles.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Marconi
{
    namespace Files
    {
        namespace fs = std::filesystem;

        namespace
        {
            fs::path ToPath(const std::string& path)
            {
                return fs::u8path(path);
            }

            // UTF-8 text of a path with '/' separators (C++20 returns u8string, C++17 string).
            std::string ToText(const fs::path& path)
            {
                auto text = path.generic_u8string();
                return std::string(text.begin(), text.end());
            }

            std::string Describe(const std::error_code& code)
            {
                return code.message();
            }

            int64_t ModifiedSeconds(const fs::path& path)
            {
                // std::filesystem's clock has no portable epoch in C++17, so ask the system directly.
#if defined(_WIN32)
                struct _stat64 info;
                return _wstat64(path.c_str(), &info) == 0 ? static_cast<int64_t>(info.st_mtime) : 0;
#else
                struct stat info;
                return ::stat(path.c_str(), &info) == 0 ? static_cast<int64_t>(info.st_mtime) : 0;
#endif
            }

            bool EntryFor(const fs::path& path, FileEntry& outEntry, std::string& error)
            {
                std::error_code code;
                fs::file_status status = fs::status(path, code);
                if (code || !fs::exists(status))
                {
                    error = "Couldn't find " + ToText(path);
                    return false;
                }
                outEntry.name = ToText(path.filename());
                outEntry.isFolder = fs::is_directory(status);
                outEntry.size = outEntry.isFolder ? 0 : static_cast<uint64_t>(fs::file_size(path, code));
                outEntry.modified = ModifiedSeconds(path);
                return true;
            }

            class LocalReader : public FileReader
            {
            public:
                explicit LocalReader(std::ifstream&& stream) : m_stream(std::move(stream)) {}

                ptrdiff_t Read(void* buffer, size_t size, std::string& error) override
                {
                    m_stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
                    if (m_stream.bad())
                    {
                        error = "Couldn't read the file";
                        return -1;
                    }
                    return static_cast<ptrdiff_t>(m_stream.gcount());
                }

            private:
                std::ifstream m_stream;
            };

            class LocalWriter : public FileWriter
            {
            public:
                explicit LocalWriter(std::ofstream&& stream) : m_stream(std::move(stream)) {}

                bool Write(const void* data, size_t size, std::string& error) override
                {
                    m_stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
                    if (!m_stream)
                    {
                        error = "Couldn't write the file (is the disk full?)";
                        return false;
                    }
                    return true;
                }

                bool Finish(std::string& error) override
                {
                    m_stream.close();
                    if (m_stream.fail())
                    {
                        error = "Couldn't finish writing the file";
                        return false;
                    }
                    return true;
                }

            private:
                std::ofstream m_stream;
            };
        } // namespace

        std::string LocalFiles::GetHome()
        {
#if defined(_WIN32)
            const char* home = std::getenv("USERPROFILE");
#else
            const char* home = std::getenv("HOME");
#endif
            if (home == nullptr || *home == '\0')
            {
                return ToText(fs::current_path());
            }
            return ToText(ToPath(home));
        }

        bool LocalFiles::List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error)
        {
            outEntries.clear();
            std::error_code code;
            fs::directory_iterator it(ToPath(folder), fs::directory_options::skip_permission_denied, code);
            if (code)
            {
                error = "Couldn't open " + folder + ": " + Describe(code);
                return false;
            }
            for (const fs::directory_entry& item : it)
            {
                FileEntry entry;
                std::string ignored;
                if (EntryFor(item.path(), entry, ignored)) // skips broken links
                {
                    outEntries.push_back(entry);
                }
            }
            return true;
        }

        bool LocalFiles::Stat(const std::string& path, FileEntry& outEntry, std::string& error)
        {
            return EntryFor(ToPath(path), outEntry, error);
        }

        bool LocalFiles::MakeFolder(const std::string& path, std::string& error)
        {
            std::error_code code;
            if (!fs::create_directory(ToPath(path), code))
            {
                error = "Couldn't make the folder " + path + (code ? ": " + Describe(code) : ": it already exists");
                return false;
            }
            return true;
        }

        bool LocalFiles::Rename(const std::string& from, const std::string& to, std::string& error)
        {
            std::error_code code;
            if (fs::exists(ToPath(to), code))
            {
                error = "Couldn't rename " + from + ": " + to + " already exists";
                return false;
            }
            fs::rename(ToPath(from), ToPath(to), code);
            if (code)
            {
                error = "Couldn't rename " + from + ": " + Describe(code);
                return false;
            }
            return true;
        }

        bool LocalFiles::Remove(const std::string& path, std::string& error)
        {
            std::error_code code;
            if (fs::is_directory(ToPath(path), code) || !fs::remove(ToPath(path), code))
            {
                error = "Couldn't delete " + path + (code ? ": " + Describe(code) : "");
                return false;
            }
            return true;
        }

        bool LocalFiles::RemoveFolder(const std::string& path, std::string& error)
        {
            std::error_code code;
            if (!fs::is_directory(ToPath(path), code) || !fs::remove(ToPath(path), code))
            {
                error = "Couldn't delete the folder " + path + (code ? ": " + Describe(code) : "");
                return false;
            }
            return true;
        }

        std::unique_ptr<FileReader> LocalFiles::OpenRead(const std::string& path, std::string& error)
        {
            std::ifstream stream(ToPath(path), std::ios::binary);
            if (!stream)
            {
                error = "Couldn't open " + path;
                return nullptr;
            }
            return std::make_unique<LocalReader>(std::move(stream));
        }

        std::unique_ptr<FileWriter> LocalFiles::OpenWrite(const std::string& path, bool overwrite, std::string& error)
        {
            std::error_code code;
            if (!overwrite && fs::exists(ToPath(path), code))
            {
                error = path + " already exists";
                return nullptr;
            }
            std::ofstream stream(ToPath(path), std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Couldn't create " + path;
                return nullptr;
            }
            return std::make_unique<LocalWriter>(std::move(stream));
        }
    } // namespace Files
} // namespace Marconi
