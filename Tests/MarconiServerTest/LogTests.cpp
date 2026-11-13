// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The server's log files: one per run, pruned, flushed per line, quiet when the folder can't be written

#include "TestSupport.h"

#include <MarconiServerCore/LoggingInputSink.h>
#include <MarconiServerCore/ServerLog.h>

#include <Log/Log.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MarconiServerTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        // A fresh empty folder under the system's temporary folder, removed when it goes out of scope.
        class TempFolder
        {
        public:
            TempFolder()
            {
                m_path = fs::temp_directory_path() /
                         ("marconi-log-test-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                fs::remove_all(m_path);
                fs::create_directories(m_path);
            }
            ~TempFolder()
            {
                std::error_code error;
                fs::remove_all(m_path, error);
            }
            const fs::path& Path() const { return m_path; }

        private:
            fs::path m_path;
        };

        std::string ReadFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        void Touch(const fs::path& path)
        {
            std::ofstream file(path);
            file << "old\n";
        }
    } // namespace

    void RunLogTests()
    {
        RunTest("A log file is made in the folder, named with the time, and each line is in it at once", []() {
            TempFolder temp;
            fs::path logs = temp.Path() / "data" / "logs"; // the folder does not exist yet
            Server::ServerLogFile file;
            CHECK(file.Open(logs.string(), "TestProgram"));
            CHECK(file.IsOpen());
            LOG_INFO("Chan", "hello %d", 42);
            LOG_WARNING("Other", "careful");
            // Still open: the lines must already be on disk, as after a crash.
            std::vector<std::string> names = Server::ListLogFiles(logs.string(), "TestProgram");
            CHECK(names.size() == 1);
            std::string text = names.empty() ? std::string() : ReadFile(logs / names[0]);
            CHECK(text.find("[Chan] hello 42") != std::string::npos);
            CHECK(text.find("[Other] WARNING: careful") != std::string::npos);
            file.Close();
            CHECK(!file.IsOpen());
        });

        RunTest("Opening a log file keeps only the newest 20 of that program's files", []() {
            TempFolder temp;
            for (int i = 1; i <= 25; ++i)
            {
                char name[64];
                std::snprintf(name, sizeof(name), "Prog_2020-01-%02d_10-00-00.log", i);
                Touch(temp.Path() / name);
            }
            Touch(temp.Path() / "Other_2020-01-01_10-00-00.log"); // another program's: left alone
            Touch(temp.Path() / "Prog_notes.log");                // not a log of ours: left alone
            Touch(temp.Path() / "Prog_2020-01-01_10-00-00.txt");
            Server::ServerLogFile file;
            CHECK(file.Open(temp.Path().string(), "Prog"));
            LOG_INFO("Chan", "x");
            file.Close();
            std::vector<std::string> names = Server::ListLogFiles(temp.Path().string(), "Prog");
            CHECK(names.size() == Server::kLogFilesKept);
            CHECK(names.size() == 20);
            // The oldest were removed, the newest old ones kept, and the new file is there as well.
            CHECK(!fs::exists(temp.Path() / "Prog_2020-01-01_10-00-00.log"));
            CHECK(!fs::exists(temp.Path() / "Prog_2020-01-06_10-00-00.log"));
            CHECK(fs::exists(temp.Path() / "Prog_2020-01-07_10-00-00.log"));
            CHECK(fs::exists(temp.Path() / "Prog_2020-01-25_10-00-00.log"));
            CHECK(names.back().compare(0, 10, "Prog_2020-") != 0 || names.back() > "Prog_2020-01-25"); // the new one
            CHECK(fs::exists(temp.Path() / "Other_2020-01-01_10-00-00.log"));
            CHECK(fs::exists(temp.Path() / "Prog_notes.log"));
            CHECK(fs::exists(temp.Path() / "Prog_2020-01-01_10-00-00.txt"));
        });

        RunTest("A folder that can't be made leaves logging off, quietly", []() {
            TempFolder temp;
            Touch(temp.Path() / "afile");
            Server::ServerLogFile file;
            // Under a plain file there can be no folder.
            CHECK(!file.Open((temp.Path() / "afile" / "logs").string(), "Prog"));
            CHECK(!file.IsOpen());
            LOG_INFO("Chan", "still fine without a file");
            file.Close(); // harmless
            CHECK(Server::ListLogFiles((temp.Path() / "afile" / "logs").string(), "Prog").empty());
        });

        RunTest("The logs folder is <data folder>/logs", []() {
            CHECK(Server::GetLogDirectory("/x/data") == (fs::path("/x/data") / "logs").string());
        });

        RunTest("The input sink for a server with nothing to control never logs what was typed", []() {
            TempFolder temp;
            Server::ServerLogFile file;
            CHECK(file.Open(temp.Path().string(), "Prog"));
            Server::LoggingInputSink sink;
            sink.TypeText("hunter2-secret");
            sink.SendKey(0x68, 0x23, true);
            sink.MovePointer(123, 456, 1, 0, 0);
            std::vector<std::string> names = Server::ListLogFiles(temp.Path().string(), "Prog");
            CHECK(names.size() == 1);
            std::string text = names.empty() ? std::string() : ReadFile(temp.Path() / names[0]);
            file.Close();
            CHECK(text.find("hunter2") == std::string::npos);
            CHECK(text.find("0x68") == std::string::npos);
            CHECK(text.find("123") == std::string::npos);
            CHECK(text.find("input") != std::string::npos); // it says once that input is arriving
        });
    }
} // namespace MarconiServerTest
