// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A Marconi server's files, over the Marconi connection

#include <MarconiCore/Files/MarconiFiles.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>

namespace Marconi
{
    namespace Files
    {
        using Wire::FileStatus;

        namespace
        {
            constexpr uint32_t kChunk = 256 * 1024; // the size of one range in a copy
            constexpr int kReadAhead = 2;           // ranges asked for before the first has come
            constexpr auto kAnswerTimeout = std::chrono::seconds(120);

            std::string Problem(const std::string& error, const char* fallback)
            {
                return error.empty() ? fallback : error;
            }
        } // namespace

        // Reads a file range by range, with the next range already on its way while the caller uses this one.
        class MarconiFiles::ReaderImpl : public FileReader
        {
        public:
            ReaderImpl(MarconiFiles& files, uint32_t handle, uint64_t offset)
                : m_files(files), m_handle(handle), m_nextOffset(offset)
            {
            }

            ~ReaderImpl() override
            {
                for (const Flight& flight : m_flights)
                {
                    m_files.Forget(flight.id);
                }
                m_files.CloseHandle(m_handle);
            }

            ptrdiff_t Read(void* buffer, size_t size, std::string& error) override
            {
                while (m_position == m_buffer.size())
                {
                    if (m_ended)
                    {
                        return 0;
                    }
                    if (!Fill(error))
                    {
                        return -1;
                    }
                }
                size_t count = std::min(size, m_buffer.size() - m_position);
                std::memcpy(buffer, m_buffer.data() + m_position, count);
                m_position += count;
                return static_cast<ptrdiff_t>(count);
            }

        private:
            struct Flight
            {
                uint32_t id;
                std::shared_ptr<Slot> slot;
            };

            bool Fill(std::string& error)
            {
                while (!m_endAsked && m_flights.size() < static_cast<size_t>(kReadAhead))
                {
                    Wire::FileRead read;
                    read.id = m_files.NewId();
                    read.handle = m_handle;
                    read.offset = m_nextOffset;
                    read.length = kChunk;
                    std::shared_ptr<Slot> slot = m_files.Expect(read.id);
                    if (!m_files.SendFramed(read.Encode(), error))
                    {
                        m_files.Forget(read.id);
                        return false;
                    }
                    m_flights.push_back({read.id, slot});
                    m_nextOffset += kChunk;
                }
                Flight flight = m_flights.front();
                m_flights.pop_front();
                if (!m_files.Await(flight.id, flight.slot, error))
                {
                    return false;
                }
                const Wire::FileData& data = flight.slot->data;
                if (data.status != FileStatus::Ok)
                {
                    error = Problem(data.error, "Couldn't read the file");
                    return false;
                }
                m_buffer = data.data;
                m_position = 0;
                if (data.end)
                {
                    m_endAsked = true; // nothing beyond this is wanted
                    m_ended = true;
                    for (const Flight& later : m_flights)
                    {
                        m_files.Forget(later.id);
                    }
                    m_flights.clear();
                }
                return true;
            }

            MarconiFiles& m_files;
            uint32_t m_handle;
            uint64_t m_nextOffset;
            std::deque<Flight> m_flights;
            Wire::Bytes m_buffer;
            size_t m_position = 0;
            bool m_endAsked = false;
            bool m_ended = false;
        };

        class MarconiFiles::WriterImpl : public FileWriter
        {
        public:
            WriterImpl(MarconiFiles& files, uint32_t handle, uint64_t offset)
                : m_files(files), m_handle(handle), m_offset(offset)
            {
            }

            ~WriterImpl() override
            {
                if (!m_finished)
                {
                    m_files.CloseHandle(m_handle);
                }
            }

            bool Write(const void* data, size_t size, std::string& error) override
            {
                const uint8_t* bytes = static_cast<const uint8_t*>(data);
                while (size > 0)
                {
                    if (m_files.TakeWriteError(m_handle, error))
                    {
                        return false;
                    }
                    Wire::FileWrite write;
                    write.handle = m_handle;
                    write.offset = m_offset;
                    const size_t count = std::min<size_t>(size, kChunk);
                    write.data.assign(bytes, bytes + count);
                    if (!m_files.SendFramed(write.Encode(), error))
                    {
                        return false;
                    }
                    m_offset += count;
                    bytes += count;
                    size -= count;
                }
                return true;
            }

            bool Finish(std::string& error) override
            {
                if (m_finished)
                {
                    return true;
                }
                m_finished = true;
                Wire::FileClose close;
                close.id = m_files.NewId();
                close.handle = m_handle;
                std::shared_ptr<Slot> slot = m_files.Expect(close.id);
                if (!m_files.SendFramed(close.Encode(), error))
                {
                    m_files.Forget(close.id);
                    return false;
                }
                if (!m_files.Await(close.id, slot, error))
                {
                    return false;
                }
                std::string early;
                if (m_files.TakeWriteError(m_handle, early))
                {
                    error = early;
                    return false;
                }
                if (slot->result.status != FileStatus::Ok)
                {
                    error = Problem(slot->result.error, "Couldn't finish writing the file");
                    return false;
                }
                return true;
            }

        private:
            MarconiFiles& m_files;
            uint32_t m_handle;
            uint64_t m_offset;
            bool m_finished = false;
        };

        MarconiFiles::MarconiFiles(SendFunction send) : m_send(std::move(send))
        {
        }

        MarconiFiles::~MarconiFiles()
        {
            Close();
        }

        void MarconiFiles::Close()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
            m_changed.notify_all();
        }

        void MarconiFiles::Reopen()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = false;
            m_pending.clear();
            m_writeErrors.clear();
            m_home.clear();
            m_desktop.clear();
        }

        uint32_t MarconiFiles::NewId()
        {
            uint32_t id = m_nextId.fetch_add(1);
            while (id == 0)
            {
                id = m_nextId.fetch_add(1);
            }
            return id;
        }

        std::shared_ptr<MarconiFiles::Slot> MarconiFiles::Expect(uint32_t id)
        {
            auto slot = std::make_shared<Slot>();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending[id] = slot;
            return slot;
        }

        void MarconiFiles::Forget(uint32_t id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.erase(id);
        }

        bool MarconiFiles::Await(uint32_t id, const std::shared_ptr<Slot>& slot, std::string& error)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const bool answered =
                m_changed.wait_for(lock, kAnswerTimeout, [&]() { return slot->done || m_closed; });
            m_pending.erase(id);
            if (slot->done)
            {
                return true;
            }
            error = answered ? "The connection ended" : "The other computer didn't answer";
            return false;
        }

        bool MarconiFiles::SendFramed(const Wire::Bytes& framed, std::string& error)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_closed)
                {
                    error = "The connection ended";
                    return false;
                }
            }
            if (!m_send || !m_send(framed))
            {
                error = "The connection ended";
                return false;
            }
            return true;
        }

        bool MarconiFiles::OnMessage(Wire::Type type, Wire::Reader& body)
        {
            switch (type)
            {
            case Wire::Type::FileResult:
            {
                Wire::FileResult result = Wire::FileResult::Decode(body);
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_pending.find(result.id);
                if (it != m_pending.end())
                {
                    it->second->type = type;
                    it->second->result = std::move(result);
                    it->second->done = true;
                    m_changed.notify_all();
                }
                return true;
            }
            case Wire::Type::FileOpened:
            {
                Wire::FileOpened opened = Wire::FileOpened::Decode(body);
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_pending.find(opened.id);
                if (it != m_pending.end())
                {
                    it->second->type = type;
                    it->second->opened = std::move(opened);
                    it->second->done = true;
                    m_changed.notify_all();
                }
                return true;
            }
            case Wire::Type::FileData:
            {
                Wire::FileData data = Wire::FileData::Decode(body);
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_pending.find(data.id);
                if (it != m_pending.end())
                {
                    it->second->type = type;
                    it->second->data = std::move(data);
                    it->second->done = true;
                    m_changed.notify_all();
                }
                return true;
            }
            case Wire::Type::FileWriteFailed:
            {
                Wire::FileWriteFailed failed = Wire::FileWriteFailed::Decode(body);
                std::lock_guard<std::mutex> lock(m_mutex);
                m_writeErrors.emplace(failed.handle, Problem(failed.error, "Couldn't write the file"));
                return true;
            }
            default:
                return false;
            }
        }

        bool MarconiFiles::TakeWriteError(uint32_t handle, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_writeErrors.find(handle);
            if (it == m_writeErrors.end())
            {
                return false;
            }
            error = it->second;
            return true; // kept, so every later write and the Finish say the same
        }

        bool MarconiFiles::Call(Wire::FileRequest request, Wire::FileResult& outResult, std::string& error)
        {
            request.id = NewId();
            std::shared_ptr<Slot> slot = Expect(request.id);
            if (!SendFramed(request.Encode(), error))
            {
                Forget(request.id);
                return false;
            }
            if (!Await(request.id, slot, error))
            {
                return false;
            }
            if (slot->result.status != FileStatus::Ok)
            {
                error = Problem(slot->result.error, "That didn't work");
                return false;
            }
            outResult = std::move(slot->result);
            return true;
        }

        std::string MarconiFiles::GetHome()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_home.empty())
                {
                    return m_home;
                }
            }
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::Home;
            Wire::FileResult result;
            std::string error;
            std::string home = Call(request, result, error) && !result.text.empty() ? result.text : "/";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_home = home;
            return home;
        }

        std::string MarconiFiles::GetDesktop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_desktop.empty())
                {
                    return m_desktop;
                }
            }
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::Desktop;
            Wire::FileResult result;
            std::string error;
            if (!Call(request, result, error) || result.text.empty())
            {
                return GetHome();
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            m_desktop = result.text;
            return m_desktop;
        }

        bool MarconiFiles::List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error)
        {
            outEntries.clear();
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::List;
            request.path = folder;
            Wire::FileResult result;
            if (!Call(request, result, error))
            {
                return false;
            }
            for (const Wire::FileInfo& info : result.entries)
            {
                FileEntry entry;
                entry.name = info.name;
                entry.size = info.size;
                entry.modified = info.modified;
                entry.isFolder = info.isFolder;
                outEntries.push_back(std::move(entry));
            }
            return true;
        }

        bool MarconiFiles::Stat(const std::string& path, FileEntry& outEntry, std::string& error)
        {
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::Stat;
            request.path = path;
            Wire::FileResult result;
            if (!Call(request, result, error))
            {
                return false;
            }
            if (result.entries.size() != 1)
            {
                error = "The other computer sent something unexpected";
                return false;
            }
            outEntry.name = FileName(path);
            outEntry.size = result.entries[0].size;
            outEntry.modified = result.entries[0].modified;
            outEntry.isFolder = result.entries[0].isFolder;
            return true;
        }

        bool MarconiFiles::MakeFolder(const std::string& path, std::string& error)
        {
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::MakeFolder;
            request.path = path;
            Wire::FileResult result;
            return Call(request, result, error);
        }

        bool MarconiFiles::Rename(const std::string& from, const std::string& to, std::string& error)
        {
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::Rename;
            request.path = from;
            request.path2 = to;
            Wire::FileResult result;
            return Call(request, result, error);
        }

        bool MarconiFiles::Remove(const std::string& path, std::string& error)
        {
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::Remove;
            request.path = path;
            Wire::FileResult result;
            return Call(request, result, error);
        }

        bool MarconiFiles::RemoveFolder(const std::string& path, std::string& error)
        {
            Wire::FileRequest request;
            request.op = Wire::FileRequest::Op::RemoveFolder;
            request.path = path;
            Wire::FileResult result;
            return Call(request, result, error);
        }

        bool MarconiFiles::OpenHandle(Wire::FileOpen::Mode mode, const std::string& path, uint64_t offset,
                                      uint32_t& outHandle, uint64_t& outSize, std::string& error)
        {
            Wire::FileOpen open;
            open.id = NewId();
            open.mode = mode;
            open.path = path;
            open.offset = offset;
            std::shared_ptr<Slot> slot = Expect(open.id);
            if (!SendFramed(open.Encode(), error))
            {
                Forget(open.id);
                return false;
            }
            if (!Await(open.id, slot, error))
            {
                return false;
            }
            if (slot->opened.status != FileStatus::Ok)
            {
                error = Problem(slot->opened.error, "Couldn't open the file");
                return false;
            }
            outHandle = slot->opened.handle;
            outSize = slot->opened.size;
            return true;
        }

        void MarconiFiles::CloseHandle(uint32_t handle)
        {
            Wire::FileClose close;
            close.id = 0;
            close.handle = handle;
            std::string ignored;
            SendFramed(close.Encode(), ignored);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_writeErrors.erase(handle);
        }

        std::unique_ptr<FileReader> MarconiFiles::OpenRead(const std::string& path, std::string& error)
        {
            return OpenReadAt(path, 0, error);
        }

        std::unique_ptr<FileReader> MarconiFiles::OpenReadAt(const std::string& path, uint64_t offset,
                                                             std::string& error)
        {
            uint32_t handle = 0;
            uint64_t size = 0;
            if (!OpenHandle(Wire::FileOpen::Mode::Read, path, 0, handle, size, error))
            {
                return nullptr;
            }
            return std::make_unique<ReaderImpl>(*this, handle, offset);
        }

        std::unique_ptr<FileWriter> MarconiFiles::OpenWrite(const std::string& path, bool overwrite,
                                                            std::string& error)
        {
            uint32_t handle = 0;
            uint64_t size = 0;
            if (!OpenHandle(overwrite ? Wire::FileOpen::Mode::Overwrite : Wire::FileOpen::Mode::Create, path, 0,
                            handle, size, error))
            {
                return nullptr;
            }
            return std::make_unique<WriterImpl>(*this, handle, 0);
        }

        std::unique_ptr<FileWriter> MarconiFiles::OpenWriteResume(const std::string& path, uint64_t offset,
                                                                  std::string& error)
        {
            uint32_t handle = 0;
            uint64_t size = 0;
            if (!OpenHandle(Wire::FileOpen::Mode::Resume, path, offset, handle, size, error))
            {
                return nullptr;
            }
            return std::make_unique<WriterImpl>(*this, handle, offset);
        }
    } // namespace Files
} // namespace Marconi
