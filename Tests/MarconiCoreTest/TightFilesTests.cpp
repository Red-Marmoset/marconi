// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for TightVNC file transfer: the Tight login, the file messages, and TightFiles against a fake server

#include "TestSupport.h"

#include <MarconiCore/Files/TightFiles.h>
#include <MarconiCore/Rfb/Handshake.h>
#include <MarconiCore/Rfb/Messages.h>
#include <MarconiCore/Rfb/TightFileTransfer.h>

#include <map>
#include <set>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;
    namespace Ft = Rfb::FileTransfer;

    namespace
    {
        class RecordingSink : public Rfb::ByteSink
        {
        public:
            void Send(const Rfb::Bytes& data) override { sent.insert(sent.end(), data.begin(), data.end()); }
            Rfb::Bytes sent;
        };

        void Capability(Rfb::OutBuffer& out, uint32_t code, const char* vendor, const char* signature)
        {
            out.U32(code).Append(std::string(vendor, 4)).Append(std::string(signature, 8));
        }

        // A TightVNC 2.x server's side of the login: Tight security, VNC password, then the feature lists.
        Rfb::Bytes TightServer(bool withPassword, bool withFileTransfer)
        {
            Rfb::OutBuffer out;
            out.Append("RFB 003.008\n").U8(2).U8(Rfb::SecurityType::VncAuth).U8(Rfb::SecurityType::Tight);
            out.U32(0); // no tunnels
            if (withPassword)
            {
                out.U32(1);
                Capability(out, 2, "STDV", "VNCAUTH_");
                out.Append(std::string(16, '\x11')); // challenge
            }
            else
            {
                out.U32(0);
            }
            out.U32(0); // security result: OK
            out.U16(64).U16(32);
            Rfb::PixelFormat().Write(out);
            out.U32(5).Append("drips");
            out.U16(withFileTransfer ? 1 : 0).U16(withFileTransfer ? 1 : 0).U16(0).U16(0);
            if (withFileTransfer)
            {
                Capability(out, Ft::FileListReply, "TGHT", "FTSFLRLY");
                Capability(out, Ft::FileListRequest, "TGHT", "FTCFLRST");
            }
            return out.Data();
        }

        // Answers TightFiles' requests like a TightVNC server, from an in-memory disk.
        class FakeTightServer
        {
        public:
            explicit FakeTightServer(Files::TightFiles& files) : m_files(files)
            {
                m_disk["/C:/Users/report.txt"] = "hello";
                m_folders.insert("/");
                m_folders.insert("/C:");
                m_folders.insert("/C:/Users");
            }

            // What TightFiles sends: decode it and answer on another thread, as the connection thread would.
            bool Receive(const Rfb::Bytes& request)
            {
                Rfb::MemorySource source(request);
                Rfb::InStream in(source);
                uint32_t id = in.U32();
                Ft::Reply reply;
                switch (id)
                {
                    case Ft::FileListRequest:
                    {
                        in.U8();
                        std::string folder = ReadText(in);
                        if (!m_folders.count(folder))
                        {
                            reply = Failed("No such folder");
                            break;
                        }
                        reply.id = Ft::FileListReply;
                        Rfb::OutBuffer list;
                        std::vector<std::pair<std::string, bool>> children;
                        for (const auto& folderPath : m_folders)
                        {
                            if (folderPath != folder && Files::ParentPath(folderPath) == folder)
                            {
                                children.push_back({Files::FileName(folderPath), true});
                            }
                        }
                        for (const auto& file : m_disk)
                        {
                            if (Files::ParentPath(file.first) == folder)
                            {
                                children.push_back({Files::FileName(file.first), false});
                            }
                        }
                        list.U32(static_cast<uint32_t>(children.size()));
                        for (const auto& child : children)
                        {
                            uint64_t size = child.second ? 0 : m_disk[Files::JoinPath(folder, child.first)].size();
                            list.U32(static_cast<uint32_t>(size >> 32)).U32(static_cast<uint32_t>(size));
                            list.U32(0).U32(1700000000u).U16(child.second ? Ft::FlagFolder : 0);
                            list.U32(static_cast<uint32_t>(child.first.size() + 1)).Append(child.first);
                            list.U8(0);
                        }
                        reply.data = list.Data();
                        break;
                    }
                    case Ft::DownloadStartRequest:
                    {
                        std::string path = ReadText(in);
                        if (!m_disk.count(path))
                        {
                            reply = Failed("No such file");
                            break;
                        }
                        m_download = m_disk[path];
                        m_downloadAt = 0;
                        reply.id = Ft::DownloadStartReply;
                        break;
                    }
                    case Ft::DownloadDataRequest:
                    {
                        in.U8();
                        uint32_t size = in.U32();
                        if (m_downloadAt >= m_download.size())
                        {
                            reply.id = Ft::DownloadEndReply;
                            break;
                        }
                        size_t count = std::min<size_t>(size, std::min<size_t>(3, m_download.size() - m_downloadAt));
                        reply.id = Ft::DownloadDataReply;
                        reply.data.assign(m_download.begin() + static_cast<ptrdiff_t>(m_downloadAt),
                                          m_download.begin() + static_cast<ptrdiff_t>(m_downloadAt + count));
                        m_downloadAt += count;
                        break;
                    }
                    case Ft::UploadStartRequest:
                    {
                        m_uploadPath = ReadText(in);
                        uint8_t flags = in.U8();
                        if (m_disk.count(m_uploadPath) && !(flags & 1))
                        {
                            reply = Failed("Already exists");
                            break;
                        }
                        m_disk[m_uploadPath].clear();
                        reply.id = Ft::UploadStartReply;
                        break;
                    }
                    case Ft::UploadDataRequest:
                    {
                        in.U8();
                        uint32_t compressed = in.U32();
                        in.U32();
                        m_disk[m_uploadPath] += in.ReadString(compressed);
                        reply.id = Ft::UploadDataReply;
                        break;
                    }
                    case Ft::UploadEndRequest:
                        reply.id = Ft::UploadEndReply;
                        break;
                    case Ft::MakeFolderRequest:
                        m_folders.insert(ReadText(in));
                        reply.id = Ft::MakeFolderReply;
                        break;
                    case Ft::RemoveRequest:
                    {
                        std::string path = ReadText(in);
                        reply.id = m_disk.erase(path) || m_folders.erase(path) ? Ft::RemoveReply : 0;
                        if (reply.id == 0)
                        {
                            reply = Failed("No such file");
                        }
                        break;
                    }
                    case Ft::RenameRequest:
                    {
                        std::string from = ReadText(in);
                        std::string to = ReadText(in);
                        m_disk[to] = m_disk[from];
                        m_disk.erase(from);
                        reply.id = Ft::RenameReply;
                        break;
                    }
                    default:
                        return false;
                }
                ++requests;
                std::thread([this, reply]() { m_files.OnReply(reply); }).detach();
                return true;
            }

            std::map<std::string, std::string> m_disk;
            std::set<std::string> m_folders;
            int requests = 0;

        private:
            static std::string ReadText(Rfb::InStream& in)
            {
                std::string text = in.ReadString(in.U32());
                while (!text.empty() && text.back() == '\0')
                {
                    text.pop_back();
                }
                return text;
            }

            static Ft::Reply Failed(const std::string& message)
            {
                Ft::Reply reply;
                reply.id = Ft::LastRequestFailedReply;
                reply.error = message;
                return reply;
            }

            Files::TightFiles& m_files;
            std::string m_download;
            size_t m_downloadAt = 0;
            std::string m_uploadPath;
        };
    } // namespace

    void RunTightFilesTests()
    {
        RunTest("ChooseSecurityType picks Tight only when allowed", []() {
            std::vector<uint8_t> drips = {Rfb::SecurityType::VncAuth, Rfb::SecurityType::Tight};
            CHECK(Rfb::ChooseSecurityType(drips, false, false, true) == Rfb::SecurityType::Tight);
            CHECK(Rfb::ChooseSecurityType(drips, false, false, false) == Rfb::SecurityType::VncAuth);
            CHECK(Rfb::ChooseSecurityType({Rfb::SecurityType::AppleRemoteDesktop, Rfb::SecurityType::VncAuth}, false,
                                          false, true) == Rfb::SecurityType::VncAuth);
        });

        RunTest("Tight login with a VNC password, and file transfer announced", []() {
            Rfb::MemorySource source(TightServer(true, true));
            Rfb::InStream in(source);
            RecordingSink sink;
            Rfb::HandshakeResult result = Rfb::PerformHandshake(
                in, sink, true,
                [](Rfb::CredentialsKind kind, Rfb::Credentials& credentials) {
                    credentials.password = "secret";
                    return kind == Rfb::CredentialsKind::Password;
                },
                nullptr, true);
            CHECK(result.securityType == Rfb::SecurityType::Tight);
            CHECK(result.tightFileTransfer);
            CHECK(result.serverInit.name == "drips");
            // Version (12), the chosen type, the chosen login method (2: VNC password), the 16-byte response, shared.
            CHECK(sink.sent.size() == 12 + 1 + 4 + 16 + 1);
            CHECK(sink.sent[12] == Rfb::SecurityType::Tight);
            CHECK(sink.sent[13] == 0 && sink.sent[14] == 0 && sink.sent[15] == 0 && sink.sent[16] == 2);
        });

        RunTest("Tight login without a password or file transfer", []() {
            Rfb::MemorySource source(TightServer(false, false));
            Rfb::InStream in(source);
            RecordingSink sink;
            Rfb::HandshakeResult result = Rfb::PerformHandshake(
                in, sink, true, [](Rfb::CredentialsKind, Rfb::Credentials&) { return false; }, nullptr, true);
            CHECK(result.securityType == Rfb::SecurityType::Tight);
            CHECK(!result.tightFileTransfer);
            CHECK(sink.sent.size() == 12 + 1 + 1);
        });

        RunTest("File transfer messages: a request and a folder listing", []() {
            CHECK(Ft::EncodeFileListRequest("/C:/x") == HexBytes("fc000102 00 00000006 2f433a2f7800"));
            Rfb::OutBuffer reply;
            reply.U8(0).U32(0).U32(0); // filled in below
            Rfb::OutBuffer list;
            list.U32(2);
            list.U32(0).U32(5).U32(0x0000018b).U32(0xcfe56800).U16(0).U32(6).Append("a.txt").U8(0);
            list.U32(0).U32(0).U32(0).U32(0).U16(Ft::FlagFolder).U32(4).Append("sub").U8(0);
            Rfb::Bytes payload = list.Data();
            Rfb::OutBuffer message;
            message.U8(0).U32(static_cast<uint32_t>(payload.size())).U32(static_cast<uint32_t>(payload.size()));
            message.Append(std::string(payload.begin(), payload.end()));
            Rfb::MemorySource source(message.Data());
            Rfb::InStream in(source);
            Ft::Reply decoded = Ft::ReadReply(Ft::FileListReply, in);
            std::vector<Files::FileEntry> entries;
            std::string error;
            CHECK(Ft::ParseFileList(decoded.data, entries, error));
            CHECK(entries.size() == 2);
            CHECK(entries[0].name == "a.txt" && entries[0].size == 5 && !entries[0].isFolder);
            CHECK(entries[0].modified == 1700000000); // milliseconds on the wire, seconds here
            CHECK(entries[1].name == "sub" && entries[1].isFolder);
            Rfb::MemorySource failure(HexBytes("00000005 6e6f706500"));
            Rfb::InStream failureIn(failure);
            CHECK(Ft::ReadReply(Ft::LastRequestFailedReply, failureIn).error == "nope");
        });

        RunTest("TightFiles lists, downloads, uploads, renames and deletes", []() {
            std::unique_ptr<FakeTightServer> server;
            Files::TightFiles files([&server](const Rfb::Bytes& request) { return server->Receive(request); });
            server = std::make_unique<FakeTightServer>(files);
            std::string error;
            std::vector<Files::FileEntry> entries;
            CHECK(files.List("/C:/Users", entries, error));
            CHECK(entries.size() == 1 && entries[0].name == "report.txt" && entries[0].size == 5);

            std::unique_ptr<Files::FileReader> reader = files.OpenRead("/C:/Users/report.txt", error);
            std::string content;
            char buffer[16];
            ptrdiff_t got = 0;
            while (reader && (got = reader->Read(buffer, sizeof(buffer), error)) > 0)
            {
                content.append(buffer, static_cast<size_t>(got));
            }
            CHECK(got == 0 && content == "hello");
            reader.reset();

            std::unique_ptr<Files::FileWriter> writer = files.OpenWrite("/C:/Users/new.txt", false, error);
            CHECK(writer && writer->Write("abc", 3, error) && writer->Write("def", 3, error) && writer->Finish(error));
            CHECK(server->m_disk["/C:/Users/new.txt"] == "abcdef");
            CHECK(!files.OpenWrite("/C:/Users/new.txt", false, error) && !error.empty());

            Files::FileEntry entry;
            CHECK(files.Stat("/C:/Users/new.txt", entry, error) && entry.size == 6);
            CHECK(files.Rename("/C:/Users/new.txt", "/C:/Users/old.txt", error));
            CHECK(server->m_disk.count("/C:/Users/old.txt") == 1);
            CHECK(files.Remove("/C:/Users/old.txt", error) && server->m_disk.count("/C:/Users/old.txt") == 0);
            CHECK(files.MakeFolder("/C:/Users/made", error) && server->m_folders.count("/C:/Users/made") == 1);

            error.clear();
            CHECK(!files.List("/C:/Nowhere", entries, error) && error.find("No such folder") != std::string::npos);
        });

        RunTest("TightFiles fails cleanly when the connection goes", []() {
            Files::TightFiles files([](const Rfb::Bytes&) { return true; }); // never answers
            std::thread closer([&files]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                files.Close();
            });
            std::vector<Files::FileEntry> entries;
            std::string error;
            auto start = std::chrono::steady_clock::now();
            CHECK(!files.List("/", entries, error) && !error.empty());
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
            closer.join();
        });
    }
} // namespace MarconiTest
