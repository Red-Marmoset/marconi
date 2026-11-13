// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Answers a viewer's file messages

#include "FileService.h"

#include <algorithm>
#include <filesystem>

namespace Marconi
{
    namespace Server
    {
        namespace fs = std::filesystem;
        using Wire::FileStatus;

        namespace
        {
            fs::path ToPath(const std::string& text)
            {
#if defined(__cpp_char8_t)
                return fs::path(std::u8string(text.begin(), text.end()));
#else
                return fs::u8path(text);
#endif
            }

            bool IsFileType(Wire::Type type)
            {
                return type == Wire::Type::FileRequest || type == Wire::Type::FileOpen ||
                       type == Wire::Type::FileRead || type == Wire::Type::FileWrite || type == Wire::Type::FileClose;
            }
        } // namespace

        FileService::FileService(bool enabled, const std::string& root, SendFunction send)
            : m_enabled(enabled), m_files(NativePathStyle(), root), m_send(std::move(send))
        {
        }

        FileService::~FileService()
        {
            Stop();
        }

        void FileService::Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
                m_changed.notify_all();
            }
            if (m_thread.joinable())
            {
                m_thread.join();
            }
            m_open.clear(); // closes every file
        }

        bool FileService::Send(const Wire::Bytes& framed)
        {
            return m_send && m_send(framed);
        }

        void FileService::Enqueue(std::function<void()> run, size_t weight)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (weight > 0)
            {
                m_changed.wait(lock, [this]() { return m_stopping || m_queuedBytes <= kMaxQueuedBytes; });
            }
            if (m_stopping)
            {
                return;
            }
            if (!m_thread.joinable())
            {
                m_thread = std::thread([this]() { Run(); });
            }
            m_queuedBytes += weight;
            m_queue.push_back(Job{std::move(run), weight});
            m_changed.notify_all();
        }

        void FileService::Run()
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_changed.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
                    if (m_stopping)
                    {
                        return; // the viewer is gone: what is still queued is dropped
                    }
                    job = std::move(m_queue.front());
                    m_queue.pop_front();
                }
                job.run();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_queuedBytes -= job.weight;
                    m_changed.notify_all();
                }
            }
        }

        bool FileService::Handle(Wire::Type type, Wire::Reader& body)
        {
            if (!IsFileType(type))
            {
                return false;
            }
            switch (type)
            {
            case Wire::Type::FileRequest:
            {
                Wire::FileRequest request = Wire::FileRequest::Decode(body);
                Enqueue([this, request]() { DoRequest(request); }, 0);
                break;
            }
            case Wire::Type::FileOpen:
            {
                Wire::FileOpen open = Wire::FileOpen::Decode(body);
                Enqueue([this, open]() { DoOpen(open); }, 0);
                break;
            }
            case Wire::Type::FileRead:
            {
                Wire::FileRead read = Wire::FileRead::Decode(body);
                Enqueue([this, read]() { DoRead(read); }, 0);
                break;
            }
            case Wire::Type::FileWrite:
            {
                auto write = std::make_shared<Wire::FileWrite>(Wire::FileWrite::Decode(body));
                const size_t weight = write->data.size() + 1;
                Enqueue([this, write]() { DoWrite(*write); }, weight);
                break;
            }
            case Wire::Type::FileClose:
            {
                Wire::FileClose close = Wire::FileClose::Decode(body);
                Enqueue([this, close]() { DoClose(close); }, 0);
                break;
            }
            default:
                break;
            }
            return true;
        }

        void FileService::DoRequest(const Wire::FileRequest& request)
        {
            Wire::FileResult result;
            result.id = request.id;
            if (!m_enabled)
            {
                result.status = FileStatus::NotAllowed;
                result.error = "This computer doesn't share its files";
                Send(result.Encode());
                return;
            }
            std::string error;
            FileStatus status = FileStatus::Ok;
            switch (request.op)
            {
            case Wire::FileRequest::Op::Home:
                status = m_files.Home(result.text, error);
                break;
            case Wire::FileRequest::Op::Desktop:
                status = m_files.Desktop(result.text, error);
                break;
            case Wire::FileRequest::Op::List:
                status = m_files.List(request.path, result.entries, error);
                break;
            case Wire::FileRequest::Op::Stat:
            {
                Wire::FileInfo info;
                status = m_files.Stat(request.path, info, error);
                if (status == FileStatus::Ok)
                {
                    result.entries.push_back(info);
                }
                break;
            }
            case Wire::FileRequest::Op::MakeFolder:
                status = m_files.MakeFolder(request.path, error);
                break;
            case Wire::FileRequest::Op::Rename:
                status = m_files.Rename(request.path, request.path2, error);
                break;
            case Wire::FileRequest::Op::Remove:
                status = m_files.Remove(request.path, error);
                break;
            case Wire::FileRequest::Op::RemoveFolder:
                status = m_files.RemoveFolder(request.path, error);
                break;
            }
            result.status = status;
            if (status != FileStatus::Ok)
            {
                result.error = error.empty() ? "That didn't work" : error;
                result.entries.clear();
                result.text.clear();
            }
            Send(result.Encode());
        }

        void FileService::DoOpen(const Wire::FileOpen& open)
        {
            Wire::FileOpened opened;
            opened.id = open.id;
            auto fail = [&](FileStatus status, const std::string& error) {
                opened.status = status;
                opened.error = error;
                Send(opened.Encode());
            };
            if (!m_enabled)
            {
                fail(FileStatus::NotAllowed, "This computer doesn't share its files");
                return;
            }
            if (m_open.size() >= kMaxOpenFiles)
            {
                fail(FileStatus::Failed, "Too many files are open at once");
                return;
            }
            std::string real;
            std::string error;
            bool isRoot = false;
            FileStatus status = m_files.Resolve(open.path, real, error, &isRoot);
            if (status != FileStatus::Ok)
            {
                fail(status, error);
                return;
            }
            if (isRoot)
            {
                fail(FileStatus::BadPath, "That is a folder, not a file");
                return;
            }
            const fs::path path = ToPath(real);
            std::error_code code;
            auto file = std::make_unique<OpenFile>();
            uint64_t size = 0;

            if (open.mode == Wire::FileOpen::Mode::Read)
            {
                // Only plain files: opening a pipe or a device could wait for ever, or never end.
                if (!fs::is_regular_file(fs::status(path, code)) || code)
                {
                    fail(FileStatus::Failed, "That isn't a file that can be copied");
                    return;
                }
                size = static_cast<uint64_t>(fs::file_size(path, code));
                file->stream.open(path, std::ios::in | std::ios::binary);
                if (!file->stream)
                {
                    fail(FileStatus::Failed, "Couldn't open " + open.path);
                    return;
                }
            }
            else
            {
                file->writing = true;
                const bool exists = fs::exists(fs::symlink_status(path, code));
                if (open.mode == Wire::FileOpen::Mode::Create && exists)
                {
                    fail(FileStatus::Failed, open.path + " already exists");
                    return;
                }
                if (exists && !fs::is_regular_file(fs::status(path, code)))
                {
                    fail(FileStatus::Failed, "That isn't a file that can be written");
                    return;
                }
                if (open.mode == Wire::FileOpen::Mode::Resume)
                {
                    uint64_t have = exists ? static_cast<uint64_t>(fs::file_size(path, code)) : 0;
                    if (open.offset > have)
                    {
                        fail(FileStatus::Failed, "The file on this computer is shorter than where the copy stopped");
                        return;
                    }
                    if (exists && have > open.offset)
                    {
                        fs::resize_file(path, open.offset, code);
                        if (code)
                        {
                            fail(FileStatus::Failed, "Couldn't continue " + open.path);
                            return;
                        }
                    }
                    size = open.offset;
                    file->stream.open(path, std::ios::in | std::ios::out | std::ios::binary);
                    if (!file->stream && !exists)
                    {
                        file->stream.clear();
                        file->stream.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
                    }
                    file->position = open.offset;
                    if (file->stream)
                    {
                        file->stream.seekp(static_cast<std::streamoff>(open.offset));
                    }
                }
                else
                {
                    file->stream.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
                }
                if (!file->stream)
                {
                    fail(FileStatus::Failed, "Couldn't create " + open.path);
                    return;
                }
            }
            const uint32_t handle = m_nextHandle++;
            m_open[handle] = std::move(file);
            opened.status = FileStatus::Ok;
            opened.handle = handle;
            opened.size = size;
            Send(opened.Encode());
        }

        void FileService::DoRead(const Wire::FileRead& read)
        {
            Wire::FileData data;
            data.id = read.id;
            auto it = m_open.find(read.handle);
            if (!m_enabled || it == m_open.end() || it->second->writing)
            {
                data.status = m_enabled ? FileStatus::Failed : FileStatus::NotAllowed;
                data.error = m_enabled ? "That file isn't open for reading" : "This computer doesn't share its files";
                Send(data.Encode());
                return;
            }
            std::fstream& stream = it->second->stream;
            const uint32_t length = std::min(read.length, Wire::kMaxFileChunk);
            data.data.resize(length);
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(std::min<uint64_t>(read.offset, INT64_MAX)));
            size_t got = 0;
            if (stream && length > 0)
            {
                stream.read(reinterpret_cast<char*>(data.data.data()), static_cast<std::streamsize>(length));
                got = static_cast<size_t>(stream.gcount());
            }
            if (stream.bad())
            {
                data.status = FileStatus::Failed;
                data.error = "Couldn't read the file";
                data.data.clear();
                Send(data.Encode());
                return;
            }
            data.data.resize(got);
            data.end = got < length;
            Send(data.Encode());
        }

        void FileService::DoWrite(const Wire::FileWrite& write)
        {
            auto it = m_open.find(write.handle);
            Wire::FileWriteFailed failure;
            failure.handle = write.handle;
            if (!m_enabled || it == m_open.end() || !it->second->writing)
            {
                failure.status = m_enabled ? FileStatus::Failed : FileStatus::NotAllowed;
                failure.error = m_enabled ? "That file isn't open for writing" : "This computer doesn't share its files";
                Send(failure.Encode());
                return;
            }
            OpenFile& file = *it->second;
            if (file.failed)
            {
                return; // already reported; the close says so again
            }
            if (write.offset != file.position)
            {
                file.failed = true;
                file.error = "File data arrived out of order";
            }
            else
            {
                file.stream.write(reinterpret_cast<const char*>(write.data.data()),
                                  static_cast<std::streamsize>(write.data.size()));
                if (!file.stream)
                {
                    file.failed = true;
                    file.error = "Couldn't write the file (is the disk full?)";
                }
                else
                {
                    file.position += write.data.size();
                }
            }
            if (file.failed)
            {
                failure.status = FileStatus::Failed;
                failure.error = file.error;
                Send(failure.Encode());
            }
        }

        void FileService::DoClose(const Wire::FileClose& close)
        {
            Wire::FileResult result;
            result.id = close.id;
            auto it = m_open.find(close.handle);
            if (it == m_open.end())
            {
                result.status = FileStatus::Failed;
                result.error = "That file isn't open";
            }
            else
            {
                OpenFile& file = *it->second;
                if (file.writing)
                {
                    file.stream.flush();
                    if (file.failed)
                    {
                        result.status = FileStatus::Failed;
                        result.error = file.error;
                    }
                    else if (!file.stream)
                    {
                        result.status = FileStatus::Failed;
                        result.error = "Couldn't finish writing the file";
                    }
                }
                file.stream.close();
                m_open.erase(it);
            }
            if (close.id != 0)
            {
                Send(result.Encode());
            }
        }
    } // namespace Server
} // namespace Marconi
