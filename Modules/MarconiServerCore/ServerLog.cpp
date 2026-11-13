// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One log file per run for a server program; see ServerLog.h

#include "ServerLog.h"

#include <Log/Log.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // "YYYY-MM-DD_HH-MM-SS", the timestamp the Log module puts in a file's name.
            bool IsTimestamp(const std::string& text)
            {
                static const char kShape[] = "dddd-dd-dd_dd-dd-dd";
                if (text.size() != sizeof(kShape) - 1)
                {
                    return false;
                }
                for (size_t i = 0; i < text.size(); ++i)
                {
                    bool ok = kShape[i] == 'd' ? std::isdigit(static_cast<unsigned char>(text[i])) != 0
                                               : text[i] == kShape[i];
                    if (!ok)
                    {
                        return false;
                    }
                }
                return true;
            }

            // The Log module only writes the line to its file; flushing here makes each line reach the disk.
            void FlushAfterEachLine(Log::Level, const char*, const char*)
            {
                Log::FlushLogFile();
            }
        } // namespace

        std::string GetLogDirectory(const std::string& dataDirectory)
        {
            return (std::filesystem::path(dataDirectory) / "logs").string();
        }

        std::vector<std::string> ListLogFiles(const std::string& directory, const std::string& program)
        {
            std::vector<std::string> names;
            std::error_code error;
            const std::string prefix = program + "_";
            const std::string suffix = ".log";
            for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
                 it.increment(error))
            {
                std::string name = it->path().filename().string();
                if (name.size() > prefix.size() + suffix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                    IsTimestamp(name.substr(prefix.size(), name.size() - prefix.size() - suffix.size())))
                {
                    names.push_back(name);
                }
            }
            std::sort(names.begin(), names.end()); // the timestamp sorts oldest first
            return names;
        }

        ServerLogFile::~ServerLogFile()
        {
            Close();
        }

        bool ServerLogFile::Open(const std::string& directory, const std::string& program, size_t keep)
        {
            Close();
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                return false;
            }

            // Remove before opening, so the file about to be made is never a candidate. A file that can't be
            // removed (in use, or no permission) is left alone.
            std::vector<std::string> existing = ListLogFiles(directory, program);
            size_t room = keep > 0 ? keep - 1 : 0;
            for (size_t i = 0; i + room < existing.size(); ++i)
            {
                std::filesystem::remove(std::filesystem::path(directory) / existing[i], error);
            }

            if (!Log::InitializeFileLogging(directory, program))
            {
                return false;
            }
            Log::SetCallback(&FlushAfterEachLine);
            m_open = true;
            return true;
        }

        void ServerLogFile::Close()
        {
            if (m_open)
            {
                Log::SetCallback(nullptr);
                Log::ShutdownFileLogging();
                m_open = false;
            }
        }
    } // namespace Server
} // namespace Marconi
