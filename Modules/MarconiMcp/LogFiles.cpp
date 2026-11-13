// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Where MarconiMcp writes its log: the viewer's log folder, with only the most recent few kept

#include <MarconiMcp/LogFiles.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            // Log::InitializeFileLogging names files <appName>_<date>_<time>.log.
            const char* const kLogName = "marconi-mcp";

            std::string Environment(const char* name)
            {
                const char* value = std::getenv(name);
                return value ? std::string(value) : std::string();
            }
        } // namespace

        std::string LogDirectory()
        {
#if defined(_WIN32)
            const std::string base = Environment("LOCALAPPDATA");
            return base.empty() ? std::string() : base + "\\marconi\\logs";
#elif defined(__APPLE__)
            const std::string home = Environment("HOME");
            return home.empty() ? std::string() : home + "/Library/Application Support/marconi/logs";
#else
            // wxStandardPaths::GetUserLocalDataDir: ~/.<app name>, even with the XDG file layout (which only moves
            // the settings file).
            const std::string home = Environment("HOME");
            return home.empty() ? std::string() : home + "/.marconi/logs";
#endif
        }

        bool StartFileLogging(int keep)
        {
            namespace fs = std::filesystem;
            const std::string directory = LogDirectory();
            if (directory.empty())
            {
                return false;
            }
#if !defined(_WIN32)
            // Folders and logs made from here on are private to this user, as the viewer makes them.
            umask(S_IRWXG | S_IRWXO);
#endif
            std::error_code error;
            fs::create_directories(directory, error);
            if (error)
            {
                return false;
            }

            // Every AI session starts its own MarconiMcp, so old logs pile up: keep the newest few. The names hold
            // the date and time, so sorting them sorts by age.
            std::vector<fs::path> earlier;
            for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
            {
                const std::string name = entry.path().filename().string();
                if (entry.is_regular_file(error) && name.rfind(std::string(kLogName) + "_", 0) == 0 &&
                    entry.path().extension() == ".log")
                {
                    earlier.push_back(entry.path());
                }
            }
            std::sort(earlier.begin(), earlier.end());
            const size_t keepEarlier = keep > 1 ? static_cast<size_t>(keep - 1) : 0;
            for (size_t i = 0; i + keepEarlier < earlier.size(); ++i)
            {
                fs::remove(earlier[i], error);
            }

            return Log::InitializeFileLogging(directory, kLogName);
        }
    } // namespace Mcp
} // namespace Marconi
