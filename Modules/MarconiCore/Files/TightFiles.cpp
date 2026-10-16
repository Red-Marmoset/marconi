// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A TightVNC server's files, through file transfer messages on the open VNC connection (no second login)

#include <MarconiCore/Files/TightFiles.h>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace Marconi
{
    namespace Files
    {
        namespace Ft = Rfb::FileTransfer;

        namespace
        {
            constexpr size_t kChunk = 32 * 1024;
            constexpr auto kReplyTimeout = std::chrono::seconds(60);

            bool SameNameIgnoringCase(const std::string& a, const std::string& b)
            {
                return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                           return std::tolower(static_cast<unsigned char>(x)) ==
                                  std::tolower(static_cast<unsigned char>(y));
                       });
            }

            class TightReader : public FileReader
            {
            public:
                explicit TightReader(TightFiles& files) : m_files(files) {}

                ptrdiff_t Read(void* buffer, size_t size, std::string& error) override
                {
                    if (m_offset == m_buffer.size())
                    {
                        if (m_ended)
                        {
                            return 0;
                        }
                        Ft::Reply reply;
                        if (!m_files.Request(Ft::EncodeDownloadDataRequest(static_cast<uint32_t>(kChunk)), reply,
                                             error))
                        {
                            return -1;
                        }
                        if (reply.id == Ft::DownloadEndReply)
                        {
                            m_ended = true;
                            return 0;
                        }
                        m_buffer = std::move(reply.data);
                        m_offset = 0;
                        if (m_buffer.empty())
                        {
                            return Read(buffer, size, error);
                        }
                    }
                    size_t count = std::min(size, m_buffer.size() - m_offset);
                    std::copy(m_buffer.begin() + static_cast<ptrdiff_t>(m_offset),
                              m_buffer.begin() + static_cast<ptrdiff_t>(m_offset + count),
                              static_cast<uint8_t*>(buffer));
                    m_offset += count;
                    return static_cast<ptrdiff_t>(count);
                }

            private:
                TightFiles& m_files;
                Rfb::Bytes m_buffer;
                size_t m_offset = 0;
                bool m_ended = false;
            };

            class TightWriter : public FileWriter
            {
            public:
                explicit TightWriter(TightFiles& files) : m_files(files) {}

                bool Write(const void* data, size_t size, std::string& error) override
                {
                    const uint8_t* bytes = static_cast<const uint8_t*>(data);
                    while (size > 0)
                    {
                        size_t count = std::min(size, kChunk);
                        Ft::Reply reply;
                        if (!m_files.Request(Ft::EncodeUploadDataRequest(bytes, static_cast<uint32_t>(count)), reply,
                                             error))
                        {
                            return false;
                        }
                        bytes += count;
                        size -= count;
                    }
                    return true;
                }

                bool Finish(std::string& error) override
                {
                    auto now = std::chrono::system_clock::now().time_since_epoch();
                    uint64_t milliseconds =
                        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
                    Ft::Reply reply;
                    return m_files.Request(Ft::EncodeUploadEndRequest(milliseconds), reply, error);
                }

            private:
                TightFiles& m_files;
            };
        } // namespace

        TightFiles::TightFiles(SendFunction send) : m_send(std::move(send)) {}

        void TightFiles::OnReply(const Ft::Reply& reply)
        {
            {
                std::lock_guard<std::mutex> lock(m_replyMutex);
                m_reply = reply;
            }
            m_replied.notify_all();
        }

        void TightFiles::Close()
        {
            {
                std::lock_guard<std::mutex> lock(m_replyMutex);
                m_closed = true;
            }
            m_replied.notify_all();
        }

        void TightFiles::Reopen()
        {
            std::lock_guard<std::mutex> lock(m_replyMutex);
            m_closed = false;
            m_reply.reset();
        }

        bool TightFiles::Request(const Rfb::Bytes& request, Ft::Reply& outReply, std::string& error)
        {
            std::lock_guard<std::mutex> one(m_requestMutex);
            {
                std::lock_guard<std::mutex> lock(m_replyMutex);
                if (m_closed)
                {
                    error = "The connection has closed";
                    return false;
                }
                m_reply.reset();
            }
            if (!m_send(request))
            {
                error = "The connection has closed";
                return false;
            }
            std::unique_lock<std::mutex> lock(m_replyMutex);
            if (!m_replied.wait_for(lock, kReplyTimeout, [this]() { return m_reply.has_value() || m_closed; }))
            {
                error = "The server didn't answer";
                return false;
            }
            if (!m_reply)
            {
                error = "The connection has closed";
                return false;
            }
            outReply = *m_reply;
            m_reply.reset();
            if (outReply.id == Ft::LastRequestFailedReply)
            {
                error = outReply.error.empty() ? std::string("The server refused") : outReply.error;
                return false;
            }
            return true;
        }

        std::string TightFiles::GetHome()
        {
            return "/"; // TightVNC lists the drives there
        }

        bool TightFiles::List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error)
        {
            Ft::Reply reply;
            if (!Request(Ft::EncodeFileListRequest(folder), reply, error))
            {
                error = "Couldn't open " + folder + ": " + error;
                return false;
            }
            return Ft::ParseFileList(reply.data, outEntries, error);
        }

        bool TightFiles::Stat(const std::string& path, FileEntry& outEntry, std::string& error)
        {
            // TightVNC has no "stat": find the entry in its folder's listing.
            std::string name = FileName(path);
            std::string folder = ParentPath(path);
            if (name.empty() || folder == path)
            {
                outEntry = FileEntry();
                outEntry.name = path;
                outEntry.isFolder = true;
                return true;
            }
            std::vector<FileEntry> entries;
            std::string listError;
            if (!List(folder, entries, listError))
            {
                error = "Couldn't find " + path;
                return false;
            }
            for (bool ignoreCase : {false, true}) // Windows names ignore case
            {
                for (const FileEntry& entry : entries)
                {
                    if (ignoreCase ? SameNameIgnoringCase(entry.name, name) : entry.name == name)
                    {
                        outEntry = entry;
                        return true;
                    }
                }
            }
            error = "Couldn't find " + path;
            return false;
        }

        bool TightFiles::MakeFolder(const std::string& path, std::string& error)
        {
            Ft::Reply reply;
            return Request(Ft::EncodeMakeFolderRequest(path), reply, error);
        }

        bool TightFiles::Rename(const std::string& from, const std::string& to, std::string& error)
        {
            Ft::Reply reply;
            return Request(Ft::EncodeRenameRequest(from, to), reply, error);
        }

        bool TightFiles::Remove(const std::string& path, std::string& error)
        {
            Ft::Reply reply;
            return Request(Ft::EncodeRemoveRequest(path), reply, error);
        }

        bool TightFiles::RemoveFolder(const std::string& path, std::string& error)
        {
            Ft::Reply reply;
            return Request(Ft::EncodeRemoveRequest(path), reply, error);
        }

        std::unique_ptr<FileReader> TightFiles::OpenRead(const std::string& path, std::string& error)
        {
            Ft::Reply reply;
            if (!Request(Ft::EncodeDownloadStartRequest(path), reply, error))
            {
                error = "Couldn't open " + path + ": " + error;
                return nullptr;
            }
            return std::make_unique<TightReader>(*this);
        }

        std::unique_ptr<FileWriter> TightFiles::OpenWrite(const std::string& path, bool overwrite, std::string& error)
        {
            FileEntry existing;
            std::string ignored;
            if (!overwrite && Stat(path, existing, ignored))
            {
                error = path + " already exists";
                return nullptr;
            }
            Ft::Reply reply;
            if (!Request(Ft::EncodeUploadStartRequest(path, overwrite), reply, error))
            {
                error = "Couldn't create " + path + ": " + error;
                return nullptr;
            }
            return std::make_unique<TightWriter>(*this);
        }
    } // namespace Files
} // namespace Marconi
