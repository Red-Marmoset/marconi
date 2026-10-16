// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for file transfer: paths, "keep both" names, local files, and the transfer queue between two local folders

#include "TestSupport.h"

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Files/LocalFiles.h>
#include <MarconiCore/Files/SftpFiles.h>
#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Files/TransferQueue.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        // A fresh empty folder under the system temp folder, removed afterwards.
        class TempFolder
        {
        public:
            TempFolder()
            {
                std::random_device random;
                m_path = fs::temp_directory_path() / ("marconi-files-test-" + std::to_string(random()));
                fs::create_directories(m_path);
            }
            ~TempFolder()
            {
                std::error_code ignored;
                fs::remove_all(m_path, ignored);
            }
            std::string Path() const
            {
                auto text = m_path.generic_u8string();
                return std::string(text.begin(), text.end());
            }

        private:
            fs::path m_path;
        };

        fs::path ToPath(const std::string& path)
        {
#if defined(__cpp_char8_t)
            return fs::path(std::u8string(path.begin(), path.end()));
#else
            return fs::u8path(path);
#endif
        }

        void WriteFile(const std::string& path, const std::string& content)
        {
            std::ofstream out(ToPath(path), std::ios::binary);
            out << content;
        }

        std::string ReadFile(const std::string& path)
        {
            std::ifstream in(ToPath(path), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        bool Exists(const std::string& path)
        {
            return fs::exists(ToPath(path));
        }

        // Collects statuses and answers conflicts with a fixed reply, counting how often it was asked.
        struct Recorder
        {
            std::mutex mutex;
            std::vector<Files::TransferStatus> statuses;
            Files::ConflictAnswer answer;
            std::atomic<int> asked{0};

            Files::TransferQueue::StatusFunction Status()
            {
                return [this](const Files::TransferStatus& status) {
                    std::lock_guard<std::mutex> lock(mutex);
                    statuses.push_back(status);
                };
            }
            Files::TransferQueue::ConflictFunction Conflict()
            {
                return [this](const std::string&) {
                    ++asked;
                    return answer;
                };
            }
            Files::TransferState Final(int id)
            {
                std::lock_guard<std::mutex> lock(mutex);
                Files::TransferState state = Files::TransferState::Waiting;
                for (const Files::TransferStatus& status : statuses)
                {
                    if (status.id == id)
                    {
                        state = status.state;
                    }
                }
                return state;
            }
        };
    } // namespace

    void RunFilesTests()
    {
        RunTest("JoinPath, ParentPath and FileName", []() {
            CHECK(Files::JoinPath("/Users/sem", "a.txt") == "/Users/sem/a.txt");
            CHECK(Files::JoinPath("/Users/sem/", "a.txt") == "/Users/sem/a.txt");
            CHECK(Files::JoinPath("/", "Users") == "/Users");
            CHECK(Files::JoinPath("C:/", "Users") == "C:/Users");
            CHECK(Files::ParentPath("/Users/sem") == "/Users");
            CHECK(Files::ParentPath("/Users") == "/");
            CHECK(Files::ParentPath("/") == "/");
            CHECK(Files::ParentPath("C:/Users") == "C:/");
            CHECK(Files::ParentPath("C:/") == "C:/");
            CHECK(Files::FileName("/Users/sem/a.txt") == "a.txt");
            CHECK(Files::FileName("C:/x/") == "x");
        });

        RunTest("KeepBothName picks the first free numbered name", []() {
            auto taken = [](const std::string& name) {
                return name == "report.pdf" || name == "report (2).pdf";
            };
            CHECK(Files::KeepBothName("report.pdf", taken) == "report (3).pdf");
            CHECK(Files::KeepBothName("notes", [](const std::string& n) { return n == "notes"; }) == "notes (2)");
            CHECK(Files::KeepBothName(".bashrc", [](const std::string& n) { return n == ".bashrc"; }) ==
                  ".bashrc (2)");
            CHECK(Files::KeepBothName("a.tar.gz", [](const std::string& n) { return n == "a.tar.gz"; }) ==
                  "a.tar (2).gz");
        });

        RunTest("LocalFiles lists, reads, writes, renames and deletes", []() {
            TempFolder temp;
            Files::LocalFiles local;
            std::string error;
            CHECK(!local.GetHome().empty());
            WriteFile(temp.Path() + "/a.txt", "hello");
            CHECK(local.MakeFolder(temp.Path() + "/sub", error));
            std::vector<Files::FileEntry> entries;
            CHECK(local.List(temp.Path(), entries, error));
            CHECK(entries.size() == 2);
            bool sawFile = false;
            bool sawFolder = false;
            for (const Files::FileEntry& entry : entries)
            {
                sawFile |= entry.name == "a.txt" && !entry.isFolder && entry.size == 5 && entry.modified > 0;
                sawFolder |= entry.name == "sub" && entry.isFolder;
            }
            CHECK(sawFile && sawFolder);

            std::unique_ptr<Files::FileWriter> writer = local.OpenWrite(temp.Path() + "/b.txt", false, error);
            CHECK(writer && writer->Write("abc", 3, error) && writer->Finish(error));
            CHECK(ReadFile(temp.Path() + "/b.txt") == "abc");
            CHECK(!local.OpenWrite(temp.Path() + "/b.txt", false, error)); // exists, no overwrite
            CHECK(!error.empty());

            std::unique_ptr<Files::FileReader> reader = local.OpenRead(temp.Path() + "/a.txt", error);
            char buffer[16] = {};
            CHECK(reader && reader->Read(buffer, sizeof(buffer), error) == 5);
            CHECK(std::string(buffer, 5) == "hello");
            reader.reset();

            CHECK(local.Rename(temp.Path() + "/b.txt", temp.Path() + "/c.txt", error));
            CHECK(Exists(temp.Path() + "/c.txt") && !Exists(temp.Path() + "/b.txt"));
            CHECK(local.Remove(temp.Path() + "/c.txt", error) && !Exists(temp.Path() + "/c.txt"));
            CHECK(local.RemoveFolder(temp.Path() + "/sub", error) && !Exists(temp.Path() + "/sub"));
            Files::FileEntry entry;
            CHECK(!local.Stat(temp.Path() + "/missing", entry, error) && !error.empty());
        });

        RunTest("TransferQueue copies files and whole folders", []() {
            TempFolder from;
            TempFolder to;
            WriteFile(from.Path() + "/one.txt", "1");
            fs::create_directories(ToPath(from.Path() + "/folder/inner"));
            WriteFile(from.Path() + "/folder/two.txt", "22");
            WriteFile(from.Path() + "/folder/inner/three.txt", std::string(100000, 'x'));
            Files::LocalFiles local;
            Recorder recorder;
            {
                Files::TransferQueue queue(recorder.Status(), recorder.Conflict());
                std::vector<int> ids =
                    queue.Add(local, {from.Path() + "/one.txt", from.Path() + "/folder"}, local, to.Path());
                queue.WaitUntilIdle();
                CHECK(ids.size() == 2);
                CHECK(recorder.Final(ids[0]) == Files::TransferState::Done);
                CHECK(recorder.Final(ids[1]) == Files::TransferState::Done);
            }
            CHECK(ReadFile(to.Path() + "/one.txt") == "1");
            CHECK(ReadFile(to.Path() + "/folder/two.txt") == "22");
            CHECK(ReadFile(to.Path() + "/folder/inner/three.txt") == std::string(100000, 'x'));
            CHECK(recorder.asked == 0);
        });

        RunTest("TransferQueue asks about existing files: replace, skip, keep both, for the rest", []() {
            TempFolder from;
            TempFolder to;
            WriteFile(from.Path() + "/a.txt", "new");
            WriteFile(from.Path() + "/b.txt", "new");
            WriteFile(to.Path() + "/a.txt", "old");
            WriteFile(to.Path() + "/b.txt", "old");
            Files::LocalFiles local;

            Recorder replace;
            replace.answer = {Files::Conflict::Replace, false};
            {
                Files::TransferQueue queue(replace.Status(), replace.Conflict());
                queue.Add(local, {from.Path() + "/a.txt"}, local, to.Path());
                queue.WaitUntilIdle();
            }
            CHECK(ReadFile(to.Path() + "/a.txt") == "new" && replace.asked == 1);

            Recorder skip;
            skip.answer = {Files::Conflict::Skip, false};
            int skipped = 0;
            {
                Files::TransferQueue queue(skip.Status(), skip.Conflict());
                skipped = queue.Add(local, {from.Path() + "/b.txt"}, local, to.Path())[0];
                queue.WaitUntilIdle();
            }
            CHECK(ReadFile(to.Path() + "/b.txt") == "old" && skip.Final(skipped) == Files::TransferState::Skipped);

            Recorder both;
            both.answer = {Files::Conflict::KeepBoth, true}; // and for the rest of this copy
            {
                Files::TransferQueue queue(both.Status(), both.Conflict());
                queue.Add(local, {from.Path() + "/a.txt", from.Path() + "/b.txt"}, local, to.Path());
                queue.WaitUntilIdle();
            }
            CHECK(both.asked == 1);
            CHECK(ReadFile(to.Path() + "/a (2).txt") == "new" && ReadFile(to.Path() + "/b (2).txt") == "new");
            CHECK(ReadFile(to.Path() + "/b.txt") == "old");
        });

        RunTest("TransferQueue cancels and leaves no partial file", []() {
            TempFolder from;
            TempFolder to;
            WriteFile(from.Path() + "/big.bin", std::string(64 * 1024 * 1024, 'b'));
            Files::LocalFiles local;
            Recorder recorder;
            int id = 0;
            {
                Files::TransferQueue queue(recorder.Status(), recorder.Conflict());
                id = queue.Add(local, {from.Path() + "/big.bin"}, local, to.Path())[0];
                queue.Cancel();
                queue.WaitUntilIdle();
            }
            CHECK(recorder.Final(id) == Files::TransferState::Cancelled);
            CHECK(!Exists(to.Path() + "/big.bin"));
        });

        RunTest("TransferQueue reports a missing source as failed and carries on", []() {
            TempFolder from;
            TempFolder to;
            WriteFile(from.Path() + "/ok.txt", "ok");
            Files::LocalFiles local;
            Recorder recorder;
            std::vector<int> ids;
            {
                Files::TransferQueue queue(recorder.Status(), recorder.Conflict());
                ids = queue.Add(local, {from.Path() + "/missing.txt", from.Path() + "/ok.txt"}, local, to.Path());
                queue.WaitUntilIdle();
            }
            CHECK(recorder.Final(ids[0]) == Files::TransferState::Failed);
            CHECK(recorder.Final(ids[1]) == Files::TransferState::Done);
            CHECK(ReadFile(to.Path() + "/ok.txt") == "ok");
        });

        RunTest("SftpFiles fails cleanly without an SSH server", []() {
            uint16_t closedPort = 0;
            {
                Net::Socket unused;
                std::string error;
                unused.Listen("127.0.0.1", 0, error);
                closedPort = unused.GetLocalPort();
            }
            std::string error;
            Files::SftpFiles refused;
            CHECK(!refused.Connect("127.0.0.1", closedPort, 2000, error) && !error.empty());

            Net::Socket listener;
            CHECK(listener.Listen("127.0.0.1", 0, error));
            std::thread web([&listener]() {
                Net::Socket client;
                std::string ignored;
                if (listener.Accept(client, 3000, ignored))
                {
                    const char reply[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
                    client.SendAll(reply, sizeof(reply) - 1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
            Files::SftpFiles notSsh;
            error.clear();
            CHECK(!notSsh.Connect("127.0.0.1", listener.GetLocalPort(), 2000, error) && !error.empty());
            web.join();

            Files::SftpFiles never;
            std::vector<Files::FileEntry> entries;
            error.clear();
            CHECK(!never.Login("user", "password", error) && !error.empty());
            CHECK(!never.List("/", entries, error));
        });
    }
} // namespace MarconiTest
