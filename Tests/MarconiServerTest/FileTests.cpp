// Copyright (c) 2026 Red Marmoset Studios Ltd.
// File transfer: how paths are read, what the server lets a viewer reach, and the real client against the real
// server over a loopback socket, in a temporary folder (listing, upload, download, a big file, an interrupted copy,
// bad paths)

#include "TestSupport.h"

#include <MarconiCore/Files/MarconiFiles.h>
#include <MarconiCore/Wire/WireClient.h>
#include <MarconiCore/Wire/WireFileMessages.h>
#include <MarconiServerCore/FilePaths.h>
#include <MarconiServerCore/FileService.h>
#include <MarconiServerCore/GuardedFiles.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace MarconiServerTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        // A fresh folder in the system's temporary place, removed (with everything in it) when it goes.
        class TempDir
        {
        public:
            TempDir()
            {
                static int counter = 0;
                m_path = fs::temp_directory_path() /
                         ("marconi-files-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "-" + std::to_string(++counter));
                fs::create_directories(m_path);
                std::error_code code;
                m_path = fs::canonical(m_path, code);
            }
            ~TempDir()
            {
                std::error_code code;
                fs::remove_all(m_path, code);
            }
            const fs::path& Path() const { return m_path; }
            std::string Text() const { return m_path.generic_string(); }
            std::string Sub(const std::string& name) const { return (m_path / name).generic_string(); }

        private:
            fs::path m_path;
        };

        fs::path U8(const std::string& text)
        {
#if defined(__cpp_char8_t)
            return fs::path(std::u8string(text.begin(), text.end()));
#else
            return fs::u8path(text);
#endif
        }

        void WriteFile(const fs::path& path, const std::string& content)
        {
            std::ofstream out(path, std::ios::binary);
            out << content;
        }

        std::string ReadFile(const fs::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        // Bytes that depend on their position, so a misplaced or repeated range shows up.
        uint8_t PatternByte(uint64_t index)
        {
            uint64_t x = index * 0x9E3779B97F4A7C15ull;
            return static_cast<uint8_t>((x >> 29) ^ (index >> 8));
        }

        bool WriteRange(Files::FileWriter& writer, uint64_t from, uint64_t to, std::string& error)
        {
            std::vector<uint8_t> piece;
            for (uint64_t at = from; at < to;)
            {
                size_t count = static_cast<size_t>(std::min<uint64_t>(to - at, 100001)); // not a chunk multiple
                piece.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    piece[i] = PatternByte(at + i);
                }
                if (!writer.Write(piece.data(), count, error))
                {
                    return false;
                }
                at += count;
            }
            return true;
        }

        // Reads everything from `reader` and compares it to the pattern starting at `from`; returns the count.
        uint64_t ReadAndCompare(Files::FileReader& reader, uint64_t from, bool& same, std::string& error)
        {
            same = true;
            uint64_t total = 0;
            std::vector<uint8_t> buffer(70000);
            for (;;)
            {
                ptrdiff_t got = reader.Read(buffer.data(), buffer.size(), error);
                if (got < 0)
                {
                    same = false;
                    return total;
                }
                if (got == 0)
                {
                    return total;
                }
                for (ptrdiff_t i = 0; i < got; ++i)
                {
                    if (buffer[static_cast<size_t>(i)] != PatternByte(from + total + static_cast<uint64_t>(i)))
                    {
                        same = false;
                    }
                }
                total += static_cast<uint64_t>(got);
            }
        }

        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return predicate();
        }

        // A server with one viewer connected to it over loopback.
        class Rig
        {
        public:
            explicit Rig(const Server::Settings& given, bool offerFiles = true)
                : m_screen(8, 8), m_settings(given), m_listener(m_screen, m_input, MakeSettings(m_settings)),
                  m_client(m_listener2)
            {
                m_listener.SetFrameIntervalMs(100);
                std::string error;
                m_started = m_listener.Start(error, "127.0.0.1");
                m_serverThread = std::thread([this]() { m_listener.ServeOne(10000); });
                Wire::ClientOptions options;
                options.host = "127.0.0.1";
                options.port = m_listener.GetPort();
                options.clientName = "file test viewer";
                options.offerFiles = offerFiles;
                m_client.Connect(options);
                m_connected = m_listener2.WaitFor([&]() { return m_listener2.connected; }, 10000);
            }

            ~Rig()
            {
                m_client.Disconnect();
                m_listener2.WaitFor([&]() { return m_listener2.disconnected; }, 5000);
                m_listener.Stop();
                m_serverThread.join();
            }

            bool Ready() const { return m_started && m_connected; }
            Wire::Client& Client() { return m_client; }
            Files::MarconiFiles& Files() { return *m_client.GetFiles(); }

        private:
            static Server::Settings MakeSettings(Server::Settings settings)
            {
                settings.port = 0;
                settings.serverName = "File Test PC";
                settings.serverVersion = "0.0.0.0";
                return settings;
            }

            Server::TestPatternScreen m_screen;
            RecordingInput m_input;
            Server::Settings m_settings;
            Server::Listener m_listener;
            RecordingClientListener m_listener2;
            Wire::Client m_client;
            std::thread m_serverThread;
            bool m_started = false;
            bool m_connected = false;
        };

        Server::Settings ConfinedTo(const TempDir& dir)
        {
            Server::Settings settings;
            settings.fileRoot = dir.Text();
            return settings;
        }

        // The file service without a network: messages go in, the answers it sends are collected.
        class DirectService
        {
        public:
            DirectService(bool enabled, const std::string& root)
                : m_service(enabled, root, [this](const Wire::Bytes& framed) {
                      std::lock_guard<std::mutex> lock(m_mutex);
                      m_replies.push_back(framed);
                      m_changed.notify_all();
                      return true;
                  })
            {
            }

            void Feed(const Wire::Bytes& framed)
            {
                Wire::Reader outer(framed);
                uint64_t length = outer.Varint();
                Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
                Wire::Type type = Wire::ReadType(body);
                m_service.Handle(type, body);
            }

            // Waits for `count` answers in all, then decodes answer `index` as a Message.
            template <class Message>
            Message Reply(size_t index, size_t count)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait_for(lock, std::chrono::seconds(5), [&]() { return m_replies.size() >= count; });
                Message message;
                if (m_replies.size() > index)
                {
                    Wire::Bytes framed = m_replies[index];
                    Wire::Reader outer(framed);
                    uint64_t length = outer.Varint();
                    Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
                    Wire::ReadType(body);
                    message = Message::Decode(body);
                }
                else
                {
                    CHECK(false && "an answer didn't arrive");
                }
                return message;
            }

            Server::FileService& Service() { return m_service; }

        private:
            std::mutex m_mutex;
            std::condition_variable m_changed;
            std::vector<Wire::Bytes> m_replies;
            Server::FileService m_service;
        };

        Wire::FileRequest Request(uint32_t id, Wire::FileRequest::Op op, const std::string& path,
                                  const std::string& path2 = "")
        {
            Wire::FileRequest request;
            request.id = id;
            request.op = op;
            request.path = path;
            request.path2 = path2;
            return request;
        }
    } // namespace

    void RunFileTests()
    {
        RunTest("paths are cleaned, and anything odd is refused", []() {
            Server::PathRules posix{Server::PathStyle::Posix, false};
            Server::ParsedPath parsed;
            std::string error;
            CHECK(Server::ParsePath("/a/b/../c/./d//", posix, parsed, error));
            CHECK(Server::FormatPath(parsed, posix) == "/a/c/d");
            CHECK(Server::ParsePath("/", posix, parsed, error) && Server::FormatPath(parsed, posix) == "/");
            CHECK(Server::ParsePath("/a/..", posix, parsed, error) && Server::FormatPath(parsed, posix) == "/");
            CHECK(!Server::ParsePath("/..", posix, parsed, error));
            CHECK(!Server::ParsePath("/a/../..", posix, parsed, error));
            CHECK(!Server::ParsePath("/a/../../etc", posix, parsed, error));
            CHECK(!Server::ParsePath("", posix, parsed, error));
            CHECK(!Server::ParsePath("relative/path", posix, parsed, error));
            CHECK(!Server::ParsePath("C:/x", posix, parsed, error)); // not absolute on this style
            CHECK(!Server::ParsePath("/a\\b", posix, parsed, error));
            CHECK(!Server::ParsePath(std::string("/a\0b", 4), posix, parsed, error));
            CHECK(!Server::ParsePath("/a\nb", posix, parsed, error));
            CHECK(!Server::ParsePath("/a\xFF", posix, parsed, error));
            CHECK(!Server::ParsePath("/" + std::string(256, 'x'), posix, parsed, error));
            CHECK(Server::ParsePath("/" + std::string(255, 'x'), posix, parsed, error));
            CHECK(!Server::ParsePath("/" + std::string(Server::kMaxPathBytes, 'x'), posix, parsed, error));
            std::string longPath;
            for (int i = 0; i < 100; ++i)
            {
                longPath += "/" + std::string(60, 'd');
            }
            CHECK(!Server::ParsePath(longPath, posix, parsed, error)); // 6100 bytes
            CHECK(Server::ParsePath("/caf\xC3\xA9/\xE2\x82\xAC", posix, parsed, error));
            // "..." and a leading dot are ordinary names on this style.
            CHECK(Server::ParsePath("/.../.hidden", posix, parsed, error));
            CHECK(Server::ParsePath("/a:b/CON/x ", posix, parsed, error)); // fine on Posix
        });

        RunTest("Windows-style paths: drives, reserved names, and no way off the drive", []() {
            Server::PathRules win{Server::PathStyle::Windows, true};
            Server::ParsedPath parsed;
            std::string error;
            CHECK(Server::ParsePath("C:/Users/sem", win, parsed, error));
            CHECK(Server::FormatPath(parsed, win) == "C:/Users/sem");
            CHECK(Server::ParsePath("/c:/Users/../Windows", win, parsed, error));
            CHECK(Server::FormatPath(parsed, win) == "C:/Windows");
            CHECK(Server::ParsePath("C:", win, parsed, error) && Server::FormatPath(parsed, win) == "C:/");
            CHECK(Server::ParsePath("/", win, parsed, error) && Server::FormatPath(parsed, win) == "/");
            CHECK(!Server::ParsePath("C:/..", win, parsed, error));   // above the drive
            CHECK(!Server::ParsePath("/Users", win, parsed, error));  // no drive chosen
            CHECK(!Server::ParsePath("Users/sem", win, parsed, error));
            CHECK(!Server::ParsePath("C:foo", win, parsed, error));   // drive-relative
            CHECK(!Server::ParsePath("C:/a/D:/b", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/b.txt:stream", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/CON", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/nul.txt", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/COM1", win, parsed, error));
            CHECK(Server::ParsePath("C:/a/COM10", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/trailing.", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/trailing ", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/what?", win, parsed, error));
            CHECK(!Server::ParsePath("C:/a/b*", win, parsed, error));
            CHECK(!Server::ParsePath("\\\\server\\share", win, parsed, error));
            CHECK(!Server::ParsePath("C:\\Windows", win, parsed, error));
            CHECK(!Server::ParsePath("//?/C:/x", win, parsed, error));
            Server::PathRules inside{Server::PathStyle::Windows, false};
            CHECK(Server::ParsePath("/a/b", inside, parsed, error) && Server::FormatPath(parsed, inside) == "/a/b");
            CHECK(!Server::ParsePath("/C:/x", inside, parsed, error)); // ':' isn't allowed in a name
        });

        RunTest("a shared folder can't be left: dot-dot, links and odd paths stay inside", []() {
            TempDir dir;
            TempDir outside;
            fs::create_directories(dir.Path() / "inner");
            WriteFile(dir.Path() / "inner" / "a.txt", "hello");
            WriteFile(outside.Path() / "secret.txt", "secret");
            Server::GuardedFiles files(Server::NativePathStyle(), dir.Text());
            std::string error;
            std::string real;
            CHECK(files.IsUsable(error));
            CHECK(files.Resolve("/inner/a.txt", real, error) == Wire::FileStatus::Ok);
            CHECK(real == dir.Sub("inner/a.txt"));
            CHECK(files.Resolve("/inner/../inner/a.txt", real, error) == Wire::FileStatus::Ok);
            CHECK(files.Resolve("/../outside", real, error) == Wire::FileStatus::BadPath);
            CHECK(files.Resolve("/inner/../../outside", real, error) == Wire::FileStatus::BadPath);
            CHECK(files.Resolve(outside.Text(), real, error) == Wire::FileStatus::Ok); // just a name under the root
            CHECK(real == dir.Text() + outside.Text());
            CHECK(real.find(outside.Text() + "/secret") == std::string::npos);

            std::vector<Wire::FileInfo> entries;
            CHECK(files.List("/", entries, error) == Wire::FileStatus::Ok);
            CHECK(entries.size() == 1 && entries[0].name == "inner" && entries[0].isFolder);
            std::string home;
            CHECK(files.Home(home, error) == Wire::FileStatus::Ok && home == "/");

            std::string notThere;
            CHECK(files.List("/nothing-here", entries, notThere) == Wire::FileStatus::Failed);
            CHECK(notThere.find(dir.Text()) == std::string::npos); // the real place isn't shown

            CHECK(files.Remove("/", error) == Wire::FileStatus::BadPath);
            CHECK(files.RemoveFolder("/", error) == Wire::FileStatus::BadPath);
            CHECK(files.Rename("/", "/x", error) == Wire::FileStatus::BadPath);
            CHECK(files.MakeFolder("/", error) == Wire::FileStatus::Failed);

#if !defined(_WIN32)
            // A link inside the folder that points outside it.
            fs::create_directory_symlink(outside.Path(), dir.Path() / "escape");
            fs::create_symlink(outside.Path() / "secret.txt", dir.Path() / "secret-link");
            CHECK(files.Resolve("/escape/secret.txt", real, error) == Wire::FileStatus::BadPath);
            CHECK(files.Resolve("/secret-link", real, error) == Wire::FileStatus::BadPath);
            CHECK(files.Resolve("/escape/new.txt", real, error) == Wire::FileStatus::BadPath);
            CHECK(files.List("/escape", entries, error) == Wire::FileStatus::BadPath);
            CHECK(files.List("/", entries, error) == Wire::FileStatus::Ok);
            CHECK(entries.size() == 1); // the links aren't listed
            // A link that stays inside is fine.
            fs::create_symlink(dir.Path() / "inner" / "a.txt", dir.Path() / "inside-link");
            CHECK(files.Resolve("/inside-link", real, error) == Wire::FileStatus::Ok);
            CHECK(files.List("/", entries, error) == Wire::FileStatus::Ok && entries.size() == 2);
#endif
            Server::GuardedFiles broken(Server::NativePathStyle(), dir.Sub("no-such-folder"));
            CHECK(!broken.IsUsable(error));
            CHECK(broken.Resolve("/", real, error) == Wire::FileStatus::Failed);
        });

        RunTest("the file service answers each request, and refuses misuse of handles", []() {
            TempDir dir;
            WriteFile(dir.Path() / "a.txt", "0123456789");
            DirectService direct(true, dir.Text());
            // Home and a listing.
            direct.Feed(Request(1, Wire::FileRequest::Op::Home, "").Encode());
            Wire::FileResult home = direct.Reply<Wire::FileResult>(0, 1);
            CHECK(home.id == 1 && home.status == Wire::FileStatus::Ok && home.text == "/");
            direct.Feed(Request(2, Wire::FileRequest::Op::List, "/").Encode());
            Wire::FileResult listing = direct.Reply<Wire::FileResult>(1, 2);
            CHECK(listing.entries.size() == 1 && listing.entries[0].name == "a.txt" && listing.entries[0].size == 10);

            // Open to read; reading a writer's handle or an unknown handle is refused.
            Wire::FileOpen open;
            open.id = 3;
            open.path = "/a.txt";
            direct.Feed(open.Encode());
            Wire::FileOpened opened = direct.Reply<Wire::FileOpened>(2, 3);
            CHECK(opened.status == Wire::FileStatus::Ok && opened.size == 10 && opened.handle != 0);
            Wire::FileRead read;
            read.id = 4;
            read.handle = opened.handle;
            read.offset = 2;
            read.length = 100;
            direct.Feed(read.Encode());
            Wire::FileData data = direct.Reply<Wire::FileData>(3, 4);
            CHECK(data.status == Wire::FileStatus::Ok && data.end && data.data.size() == 8 && data.data[0] == '2');
            read.id = 5;
            read.handle = 999;
            direct.Feed(read.Encode());
            CHECK(direct.Reply<Wire::FileData>(4, 5).status == Wire::FileStatus::Failed);

            // A write on a read handle fails; a write out of order fails and the close says so.
            Wire::FileWrite write;
            write.handle = opened.handle;
            write.data = {1};
            direct.Feed(write.Encode());
            CHECK(direct.Reply<Wire::FileWriteFailed>(5, 6).status == Wire::FileStatus::Failed);

            Wire::FileOpen create;
            create.id = 6;
            create.mode = Wire::FileOpen::Mode::Create;
            create.path = "/new.bin";
            direct.Feed(create.Encode());
            Wire::FileOpened writer = direct.Reply<Wire::FileOpened>(6, 7);
            CHECK(writer.status == Wire::FileStatus::Ok);
            Wire::FileWrite first;
            first.handle = writer.handle;
            first.offset = 0;
            first.data = {1, 2, 3};
            direct.Feed(first.Encode());
            Wire::FileWrite skipped;
            skipped.handle = writer.handle;
            skipped.offset = 100; // leaves a hole: refused
            skipped.data = {9};
            direct.Feed(skipped.Encode());
            CHECK(direct.Reply<Wire::FileWriteFailed>(7, 8).status == Wire::FileStatus::Failed);
            Wire::FileClose close;
            close.id = 7;
            close.handle = writer.handle;
            direct.Feed(close.Encode());
            Wire::FileResult closed = direct.Reply<Wire::FileResult>(8, 9);
            CHECK(closed.status == Wire::FileStatus::Failed);
            CHECK(ReadFile(dir.Path() / "new.bin") == std::string("\x01\x02\x03", 3));

            // Creating over an existing file fails; a folder can't be opened as a file.
            create.id = 8;
            create.path = "/a.txt";
            direct.Feed(create.Encode());
            CHECK(direct.Reply<Wire::FileOpened>(9, 10).status == Wire::FileStatus::Failed);
            fs::create_directory(dir.Path() / "folder");
            open.id = 9;
            open.path = "/folder";
            direct.Feed(open.Encode());
            CHECK(direct.Reply<Wire::FileOpened>(10, 11).status == Wire::FileStatus::Failed);
            open.id = 10;
            open.path = "/";
            direct.Feed(open.Encode());
            CHECK(direct.Reply<Wire::FileOpened>(11, 12).status == Wire::FileStatus::BadPath);
        });

        RunTest("the file service refuses a thirty-third open file and turns everything away when files are off", []() {
            TempDir dir;
            WriteFile(dir.Path() / "a.txt", "x");
            {
                DirectService direct(true, dir.Text());
                size_t answers = 0;
                for (uint32_t i = 1; i <= Server::FileService::kMaxOpenFiles + 1; ++i)
                {
                    Wire::FileOpen open;
                    open.id = i;
                    open.path = "/a.txt";
                    direct.Feed(open.Encode());
                    ++answers;
                }
                Wire::FileOpened last = direct.Reply<Wire::FileOpened>(Server::FileService::kMaxOpenFiles, answers);
                CHECK(last.status == Wire::FileStatus::Failed);
            }
            DirectService off(false, dir.Text());
            off.Feed(Request(1, Wire::FileRequest::Op::List, "/").Encode());
            CHECK(off.Reply<Wire::FileResult>(0, 1).status == Wire::FileStatus::NotAllowed);
            Wire::FileOpen open;
            open.id = 2;
            open.path = "/a.txt";
            off.Feed(open.Encode());
            CHECK(off.Reply<Wire::FileOpened>(1, 2).status == Wire::FileStatus::NotAllowed);
            Wire::FileOpen create;
            create.id = 3;
            create.mode = Wire::FileOpen::Mode::Create;
            create.path = "/b.txt";
            off.Feed(create.Encode());
            CHECK(off.Reply<Wire::FileOpened>(2, 3).status == Wire::FileStatus::NotAllowed);
            CHECK(!fs::exists(dir.Path() / "b.txt"));
        });

        RunTest("random and damaged file messages sent to the server do no harm", []() {
            TempDir dir;
            WriteFile(dir.Path() / "a.txt", "content");
            DirectService direct(true, dir.Text());
            uint32_t state = 0xABCDEFu;
            auto next = [&]() {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                return state;
            };
            const Wire::Type types[] = {Wire::Type::FileRequest, Wire::Type::FileOpen, Wire::Type::FileRead,
                                        Wire::Type::FileWrite, Wire::Type::FileClose};
            int refused = 0;
            for (int i = 0; i < 4000; ++i)
            {
                Wire::Bytes body(next() % 60);
                for (uint8_t& byte : body)
                {
                    byte = static_cast<uint8_t>(next() >> 24);
                }
                if (i % 3 == 0 && body.size() > 9)
                {
                    body[4] = static_cast<uint8_t>(next() % 3); // a believable operation or mode
                }
                Wire::Reader reader(body);
                try
                {
                    direct.Service().Handle(types[next() % 5], reader);
                }
                catch (const Wire::WireError&)
                {
                    ++refused;
                }
            }
            CHECK(refused > 0);
            direct.Service().Stop();
            CHECK(ReadFile(dir.Path() / "a.txt") == "content");
        });

        RunTest("a viewer lists, makes, renames and deletes over the connection", []() {
            TempDir dir;
            fs::create_directories(dir.Path() / "docs");
            WriteFile(dir.Path() / "docs" / "a.txt", "hello");
            WriteFile(dir.Path() / "top.txt", "top");
            Rig rig(ConfinedTo(dir));
            CHECK(rig.Ready());
            CHECK(rig.Client().SupportsFiles());
            Files::MarconiFiles& files = rig.Files();
            std::string error;

            CHECK(files.GetHome() == "/");
            CHECK(files.GetDesktop() == "/"); // no Desktop folder inside the shared one
            std::vector<Files::FileEntry> entries;
            CHECK(files.List("/", entries, error));
            CHECK(entries.size() == 2);
            bool sawFolder = false;
            bool sawFile = false;
            for (const Files::FileEntry& entry : entries)
            {
                sawFolder = sawFolder || (entry.name == "docs" && entry.isFolder);
                sawFile = sawFile || (entry.name == "top.txt" && !entry.isFolder && entry.size == 3 && entry.modified > 0);
            }
            CHECK(sawFolder && sawFile);

            Files::FileEntry stat;
            CHECK(files.Stat("/docs/a.txt", stat, error) && stat.name == "a.txt" && stat.size == 5);
            CHECK(!files.Stat("/docs/missing.txt", stat, error));
            CHECK(!error.empty());

            CHECK(files.MakeFolder("/docs/sub", error));
            CHECK(fs::is_directory(dir.Path() / "docs" / "sub"));
            CHECK(!files.MakeFolder("/docs/sub", error)); // already there
            CHECK(files.Rename("/docs/a.txt", "/docs/sub/b.txt", error));
            CHECK(fs::exists(dir.Path() / "docs" / "sub" / "b.txt") && !fs::exists(dir.Path() / "docs" / "a.txt"));
            CHECK(!files.Rename("/top.txt", "/docs/sub/b.txt", error)); // won't replace
            CHECK(!files.Remove("/docs/sub", error));                   // Remove is for files
            CHECK(!files.RemoveFolder("/docs/sub", error));             // not empty
            CHECK(files.Remove("/docs/sub/b.txt", error));
            CHECK(files.RemoveFolder("/docs/sub", error));
            CHECK(!fs::exists(dir.Path() / "docs" / "sub"));
            CHECK(files.List("/docs", entries, error) && entries.empty());
            CHECK(!files.List("/nowhere", entries, error));
        });

        RunTest("a viewer uploads and downloads files, small and empty and odd-named", []() {
            TempDir dir;
            Rig rig(ConfinedTo(dir));
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;

            const std::string names[] = {"small.bin", "empty.bin", "caf\xC3\xA9 \xE2\x82\xAC.txt", "with space.txt"};
            const uint64_t sizes[] = {1234, 0, 300000, 1};
            for (int i = 0; i < 4; ++i)
            {
                std::unique_ptr<Files::FileWriter> writer = files.OpenWrite("/" + names[i], false, error);
                CHECK(writer != nullptr);
                if (writer == nullptr)
                {
                    continue;
                }
                CHECK(WriteRange(*writer, 0, sizes[i], error));
                CHECK(writer->Finish(error));
                CHECK(fs::file_size(dir.Path() / U8(names[i])) == sizes[i]);

                std::unique_ptr<Files::FileReader> reader = files.OpenRead("/" + names[i], error);
                CHECK(reader != nullptr);
                bool same = false;
                if (reader != nullptr)
                {
                    CHECK(ReadAndCompare(*reader, 0, same, error) == sizes[i]);
                    CHECK(same);
                }
            }

            // A second upload to the same name fails unless told to replace it.
            CHECK(files.OpenWrite("/small.bin", false, error) == nullptr);
            CHECK(!error.empty());
            std::unique_ptr<Files::FileWriter> replace = files.OpenWrite("/small.bin", true, error);
            CHECK(replace != nullptr);
            if (replace != nullptr)
            {
                CHECK(WriteRange(*replace, 0, 10, error) && replace->Finish(error));
                CHECK(fs::file_size(dir.Path() / "small.bin") == 10);
            }

            // Reads of things that aren't there or aren't files.
            CHECK(files.OpenRead("/absent.bin", error) == nullptr);
            fs::create_directory(dir.Path() / "folder");
            CHECK(files.OpenRead("/folder", error) == nullptr);
            CHECK(files.OpenWrite("/folder", true, error) == nullptr);
            CHECK(files.OpenWrite("/no-such-folder/file.bin", false, error) == nullptr);
        });

        RunTest("a big file goes up and comes down intact", []() {
            TempDir dir;
            Rig rig(ConfinedTo(dir));
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;
            const uint64_t size = 24ull * 1024 * 1024 + 12345; // not a multiple of any chunk

            auto start = std::chrono::steady_clock::now();
            std::unique_ptr<Files::FileWriter> writer = files.OpenWrite("/big.bin", false, error);
            CHECK(writer != nullptr);
            if (writer == nullptr)
            {
                return;
            }
            CHECK(WriteRange(*writer, 0, size, error));
            CHECK(writer->Finish(error));
            auto middle = std::chrono::steady_clock::now();
            CHECK(fs::file_size(dir.Path() / "big.bin") == size);

            std::unique_ptr<Files::FileReader> reader = files.OpenRead("/big.bin", error);
            CHECK(reader != nullptr);
            bool same = false;
            if (reader != nullptr)
            {
                CHECK(ReadAndCompare(*reader, 0, same, error) == size);
                CHECK(same);
            }
            auto end = std::chrono::steady_clock::now();
            printf("  (24 MiB up in %lld ms, down in %lld ms, over loopback with TLS)\n",
                   static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(middle - start).count()),
                   static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(end - middle).count()));
        });

        RunTest("an interrupted upload carries on from where it stopped", []() {
            TempDir dir;
            Rig rig(ConfinedTo(dir));
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;
            const uint64_t size = 3ull * 1024 * 1024 + 77;
            const uint64_t half = 1000003;

            {
                std::unique_ptr<Files::FileWriter> writer = files.OpenWrite("/part.bin", false, error);
                CHECK(writer != nullptr);
                CHECK(writer && WriteRange(*writer, 0, half, error));
                // The writer goes away without Finish: the viewer gave up.
            }
            // Writes aren't answered, so wait until the server has them all on disk.
            CHECK(WaitUntil([&]() {
                std::error_code code;
                return fs::exists(dir.Path() / "part.bin") && fs::file_size(dir.Path() / "part.bin", code) == half;
            }, 5000));

            Files::FileEntry have;
            CHECK(files.Stat("/part.bin", have, error) && have.size == half);
            // The copy asks to keep only the first part it trusts and carry on from there.
            const uint64_t trusted = half - 500;
            std::unique_ptr<Files::FileWriter> writer = files.OpenWriteResume("/part.bin", trusted, error);
            CHECK(writer != nullptr);
            if (writer != nullptr)
            {
                CHECK(WriteRange(*writer, trusted, size, error));
                CHECK(writer->Finish(error));
            }
            CHECK(fs::file_size(dir.Path() / "part.bin") == size);
            std::string content = ReadFile(dir.Path() / "part.bin");
            bool same = content.size() == size;
            for (uint64_t i = 0; same && i < size; ++i)
            {
                same = static_cast<uint8_t>(content[static_cast<size_t>(i)]) == PatternByte(i);
            }
            CHECK(same);

            // Carrying on from past the end of what is there is refused, and a missing file only starts at 0.
            CHECK(files.OpenWriteResume("/part.bin", size + 1, error) == nullptr);
            CHECK(files.OpenWriteResume("/fresh.bin", 5, error) == nullptr);
            std::unique_ptr<Files::FileWriter> fresh = files.OpenWriteResume("/fresh.bin", 0, error);
            CHECK(fresh != nullptr);
            CHECK(fresh && WriteRange(*fresh, 0, 100, error) && fresh->Finish(error));
            CHECK(fs::file_size(dir.Path() / "fresh.bin") == 100);
        });

        RunTest("an interrupted download carries on from an offset", []() {
            TempDir dir;
            Rig rig(ConfinedTo(dir));
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;
            const uint64_t size = 2ull * 1024 * 1024 + 5;
            {
                std::ofstream out(dir.Path() / "d.bin", std::ios::binary);
                for (uint64_t i = 0; i < size; ++i)
                {
                    out.put(static_cast<char>(PatternByte(i)));
                }
            }
            const uint64_t from = 1048577; // not on a chunk edge
            std::unique_ptr<Files::FileReader> reader = files.OpenReadAt("/d.bin", from, error);
            CHECK(reader != nullptr);
            bool same = false;
            if (reader != nullptr)
            {
                CHECK(ReadAndCompare(*reader, from, same, error) == size - from);
                CHECK(same);
            }
            std::unique_ptr<Files::FileReader> past = files.OpenReadAt("/d.bin", size + 100, error);
            CHECK(past != nullptr);
            if (past != nullptr)
            {
                CHECK(ReadAndCompare(*past, 0, same, error) == 0);
            }
        });

        RunTest("when the viewer drops mid-upload the server keeps what arrived and carries on serving", []() {
            TempDir dir;
            {
                Rig rig(ConfinedTo(dir));
                CHECK(rig.Ready());
                std::string error;
                std::unique_ptr<Files::FileWriter> writer = rig.Files().OpenWrite("/cut.bin", false, error);
                CHECK(writer != nullptr);
                CHECK(writer && WriteRange(*writer, 0, 700000, error));
                rig.Client().Disconnect();
                // Further writes now fail instead of hanging.
                CHECK(writer && !(WriteRange(*writer, 700000, 800000, error) && writer->Finish(error)));
                CHECK(!error.empty());
                std::string listError;
                std::vector<Files::FileEntry> entries;
                CHECK(!rig.Files().List("/", entries, listError)); // nothing hangs after the connection is gone
            }
            std::error_code code;
            // Whatever arrived before the connection closed is on disk, and no more than was sent.
            uint64_t kept = fs::exists(dir.Path() / "cut.bin") ? fs::file_size(dir.Path() / "cut.bin", code) : 0;
            CHECK(kept <= 700000);
            // The server takes a new viewer straight after.
            Rig again(ConfinedTo(dir));
            CHECK(again.Ready());
            std::vector<Files::FileEntry> entries;
            std::string error;
            CHECK(again.Files().List("/", entries, error));
        });

        RunTest("bad paths from a viewer are refused with a message, and nothing outside the folder is touched", []() {
            TempDir outer;
            fs::create_directories(outer.Path() / "shared");
            const fs::path shared = outer.Path() / "shared";
            WriteFile(outer.Path() / "secret.txt", "secret");
            WriteFile(shared / "ok.txt", "ok");
            Server::Settings settings;
            settings.fileRoot = shared.generic_string();
            Rig rig(settings);
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;
            std::vector<Files::FileEntry> entries;
            Files::FileEntry entry;

            const std::string bad[] = {"/../secret.txt",   "/ok.txt/../../secret.txt", "relative.txt", "",
                                       "/a\\b",            "/..",                      std::string("/a\0b", 4),
                                       "/" + std::string(300, 'n'), std::string(5000, '/') + "x"};
            for (const std::string& path : bad)
            {
                // The viewer's own check in front of the wire may refuse some of these; the server must refuse all.
                error.clear();
                CHECK(!files.Stat(path, entry, error));
                CHECK(!error.empty());
                CHECK(!files.List(path, entries, error));
                CHECK(files.OpenRead(path, error) == nullptr);
                CHECK(files.OpenWrite(path, true, error) == nullptr);
                CHECK(!files.MakeFolder(path, error));
                CHECK(!files.Remove(path, error));
                CHECK(!files.Rename(path, "/moved", error));
                CHECK(!files.Rename("/ok.txt", path, error));
            }
            CHECK(ReadFile(outer.Path() / "secret.txt") == "secret");
            CHECK(ReadFile(shared / "ok.txt") == "ok");
            CHECK(!fs::exists(shared / "moved"));
            // Counting: nothing new next to the shared folder, nothing new in it.
            int outerCount = 0;
            for (const fs::directory_entry& e : fs::directory_iterator(outer.Path()))
            {
                (void)e;
                ++outerCount;
            }
            CHECK(outerCount == 2);
            int sharedCount = 0;
            for (const fs::directory_entry& e : fs::directory_iterator(shared))
            {
                (void)e;
                ++sharedCount;
            }
            CHECK(sharedCount == 1);

#if !defined(_WIN32)
            // A link inside the folder pointing out of it: not listed, can't be read, written or followed.
            fs::create_symlink(outer.Path() / "secret.txt", shared / "link.txt");
            fs::create_directory_symlink(outer.Path(), shared / "up");
            CHECK(files.OpenRead("/link.txt", error) == nullptr);
            CHECK(files.OpenWrite("/link.txt", true, error) == nullptr);
            CHECK(files.OpenWrite("/up/new.txt", true, error) == nullptr);
            CHECK(!files.List("/up", entries, error));
            CHECK(files.List("/", entries, error) && entries.size() == 1);
            CHECK(ReadFile(outer.Path() / "secret.txt") == "secret");
            CHECK(!fs::exists(outer.Path() / "new.txt"));

            // A named pipe would block a read for ever; it is refused.
            CHECK(::mkfifo((shared / "pipe").c_str(), 0600) == 0);
            CHECK(files.OpenRead("/pipe", error) == nullptr);
            CHECK(files.OpenWrite("/pipe", true, error) == nullptr);
#endif
        });

        RunTest("the whole computer is reachable when no folder is set, by clean absolute paths only", []() {
            TempDir dir;
            WriteFile(dir.Path() / "x.txt", "xyz");
            Server::Settings open; // no root
            Rig rig(open);
            CHECK(rig.Ready());
            Files::MarconiFiles& files = rig.Files();
            std::string error;
            std::vector<Files::FileEntry> entries;
            CHECK(files.List(dir.Text(), entries, error) && entries.size() == 1 && entries[0].name == "x.txt");
            CHECK(files.List(dir.Text() + "/./../" + dir.Path().filename().generic_string(), entries, error));
            CHECK(!files.List(dir.Text() + "/../../../../../../../../../../../../../../../../..", entries, error));
            CHECK(!files.List("/../etc", entries, error));
            CHECK(!files.List("x.txt", entries, error));
            std::string home = files.GetHome();
            CHECK(!home.empty() && home[0] != '\\');
            std::string desktop = files.GetDesktop();
            CHECK(!desktop.empty());
            std::unique_ptr<Files::FileReader> reader = files.OpenRead(dir.Sub("x.txt"), error);
            CHECK(reader != nullptr);
            char buffer[16];
            CHECK(reader && reader->Read(buffer, sizeof(buffer), error) == 3);
        });

        RunTest("with file access off, the viewer is told and nothing is shared", []() {
            TempDir dir;
            WriteFile(dir.Path() / "x.txt", "xyz");
            Server::Settings off = ConfinedTo(dir);
            off.filesEnabled = false;
            Rig rig(off);
            CHECK(rig.Ready());
            CHECK(!rig.Client().SupportsFiles());
            std::string error;
            std::vector<Files::FileEntry> entries;
            // A viewer that sends file requests anyway is answered, not ignored.
            CHECK(!rig.Files().List("/", entries, error));
            CHECK(error.find("doesn't share") != std::string::npos);
            CHECK(rig.Files().OpenRead("/x.txt", error) == nullptr);
            CHECK(rig.Files().OpenWrite("/y.txt", false, error) == nullptr);
            CHECK(!fs::exists(dir.Path() / "y.txt"));
        });

        RunTest("a viewer that doesn't offer file transfer isn't granted it", []() {
            TempDir dir;
            Rig rig(ConfinedTo(dir), false);
            CHECK(rig.Ready());
            CHECK(!rig.Client().SupportsFiles());
        });
    }
} // namespace MarconiServerTest
