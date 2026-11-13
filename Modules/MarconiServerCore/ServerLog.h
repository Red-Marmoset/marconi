// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One log file per run for a server program, in the data folder's `logs` folder, made with the engine's Log module.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // How many log files of one program are kept; opening a new one removes the oldest beyond this.
        constexpr size_t kLogFilesKept = 20;

        // The logs folder inside a data folder: <data folder>/logs.
        std::string GetLogDirectory(const std::string& dataDirectory);

        // The log files of one program in a folder (named <program>_<date>_<time>.log, as the engine's Log module
        // names them), oldest first. Files of other programs and anything else in the folder are not listed.
        std::vector<std::string> ListLogFiles(const std::string& directory, const std::string& program);

        // Sends everything logged through LOG_* to a new file <program>_<date>_<time>.log in `directory`. Each line
        // is written out at once, so a crash still leaves the last lines. Closing (or destroying) it ends the file.
        // Only one can be open per process, since the Log module has a single file.
        class ServerLogFile
        {
        public:
            ServerLogFile() = default;
            ~ServerLogFile();
            ServerLogFile(const ServerLogFile&) = delete;
            ServerLogFile& operator=(const ServerLogFile&) = delete;

            // Makes the folder if needed, removes the oldest files of this program so that at most `keep` remain
            // counting the new one, and opens the new file. False when the folder can't be made or written: then
            // nothing is logged to a file and nothing else changes (the program carries on, quietly).
            bool Open(const std::string& directory, const std::string& program, size_t keep = kLogFilesKept);
            void Close();
            bool IsOpen() const { return m_open; }

        private:
            bool m_open = false;
        };
    } // namespace Server
} // namespace Marconi
