// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Copies files and whole folders between two file sources on a worker thread, with progress and cancelling

#include <MarconiCore/Files/TransferQueue.h>

#include <chrono>

namespace Marconi
{
    namespace Files
    {
        namespace
        {
            constexpr size_t kChunk = 256 * 1024;
            constexpr auto kStatusInterval = std::chrono::milliseconds(100);
            // Shared by every queue, so two queues reporting to the same transfer list never reuse an id.
            std::atomic<int> g_nextId{1};
        } // namespace

        TransferQueue::TransferQueue(StatusFunction status, ConflictFunction conflict)
            : m_status(std::move(status)),
              m_conflict(std::move(conflict))
        {
            m_thread = std::thread([this]() { Run(); });
        }

        TransferQueue::~TransferQueue()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            Cancel();
            m_changed.notify_all();
            m_thread.join();
        }

        std::vector<int> TransferQueue::Add(FileSource& from, const std::vector<std::string>& paths, FileSource& to,
                                            const std::string& destinationFolder)
        {
            std::vector<int> ids;
            std::vector<TransferStatus> waiting;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int batch = m_nextBatch++;
                for (const std::string& path : paths)
                {
                    Item item;
                    item.id = g_nextId++;
                    item.batch = batch;
                    item.from = &from;
                    item.path = path;
                    item.to = &to;
                    item.destinationFolder = destinationFolder;
                    m_waiting.push_back(item);
                    ids.push_back(item.id);
                    TransferStatus status;
                    status.id = item.id;
                    status.name = FileName(path);
                    waiting.push_back(status);
                }
                m_cancel = false;
            }
            for (const TransferStatus& status : waiting)
            {
                m_status(status);
            }
            m_changed.notify_all();
            return ids;
        }

        void TransferQueue::Cancel()
        {
            std::deque<Item> dropped;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                dropped.swap(m_waiting);
                m_cancel = true;
            }
            for (const Item& item : dropped)
            {
                TransferStatus status;
                status.id = item.id;
                status.name = FileName(item.path);
                status.state = TransferState::Cancelled;
                m_status(status);
            }
            m_changed.notify_all();
        }

        bool TransferQueue::IsBusy() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_working || !m_waiting.empty();
        }

        void TransferQueue::WaitUntilIdle()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this]() { return !m_working && m_waiting.empty(); });
        }

        void TransferQueue::Run()
        {
            for (;;)
            {
                Item item;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_changed.wait(lock, [this]() { return m_stopping || !m_waiting.empty(); });
                    if (m_waiting.empty())
                    {
                        return; // stopping
                    }
                    item = m_waiting.front();
                    m_waiting.pop_front();
                    m_working = true;
                }
                CopyItem(item);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_working = false;
                }
                m_changed.notify_all();
            }
        }

        void TransferQueue::CopyItem(const Item& item)
        {
            TransferStatus status;
            status.id = item.id;
            status.name = FileName(item.path);
            FileEntry entry;
            if (!item.from->Stat(item.path, entry, status.error))
            {
                status.state = TransferState::Failed;
                m_status(status);
                return;
            }
            status.state = TransferState::Copying;
            status.total = MeasureTree(*item.from, item.path, entry);
            m_status(status);

            // What's already in the destination folder, from one listing (looking up each name separately is
            // slow on servers that can only list whole folders, such as TightVNC).
            std::set<std::string> existing = ListNames(*item.to, item.destinationFolder);
            bool copied = CopyTree(item, item.path, entry, item.destinationFolder, existing, status);
            if (m_cancel)
            {
                status.state = TransferState::Cancelled;
            }
            else if (copied)
            {
                status.state = status.state == TransferState::Skipped ? TransferState::Skipped : TransferState::Done;
            }
            else
            {
                status.state = TransferState::Failed;
            }
            m_status(status);
        }

        std::set<std::string> TransferQueue::ListNames(FileSource& source, const std::string& folder)
        {
            std::set<std::string> names;
            std::vector<FileEntry> entries;
            std::string ignored;
            if (source.List(folder, entries, ignored))
            {
                for (const FileEntry& entry : entries)
                {
                    names.insert(entry.name);
                }
            }
            return names;
        }

        uint64_t TransferQueue::MeasureTree(FileSource& source, const std::string& path, const FileEntry& entry)
        {
            if (!entry.isFolder)
            {
                return entry.size;
            }
            uint64_t total = 0;
            std::vector<FileEntry> children;
            std::string ignored;
            if (source.List(path, children, ignored))
            {
                for (const FileEntry& child : children)
                {
                    if (m_cancel)
                    {
                        break;
                    }
                    total += MeasureTree(source, JoinPath(path, child.name), child);
                }
            }
            return total;
        }

        std::string TransferQueue::ResolveConflict(const Item& item, const std::string& folder,
                                                   const std::string& name, const std::set<std::string>& existing,
                                                   bool& outReplace)
        {
            outReplace = false;
            std::string destination = JoinPath(folder, name);
            if (existing.count(name) == 0)
            {
                return destination; // nothing there
            }
            ConflictAnswer answer;
            if (m_answerBatch == item.batch)
            {
                answer = m_batchAnswer;
            }
            else
            {
                answer = m_conflict(destination);
                if (answer.forTheRest)
                {
                    m_answerBatch = item.batch;
                    m_batchAnswer = answer;
                }
            }
            switch (answer.choice)
            {
                case Conflict::Replace:
                    outReplace = true;
                    return destination;
                case Conflict::Skip:
                    return "";
                case Conflict::KeepBoth:
                    break;
            }
            return JoinPath(folder, KeepBothName(name, [&existing](const std::string& candidate) {
                                return existing.count(candidate) != 0;
                            }));
        }

        bool TransferQueue::CopyTree(const Item& item, const std::string& sourcePath, const FileEntry& entry,
                                     const std::string& destinationFolder, const std::set<std::string>& existing,
                                     TransferStatus& status)
        {
            if (m_cancel)
            {
                return false;
            }
            if (!entry.isFolder)
            {
                bool replace = false;
                std::string destination = ResolveConflict(item, destinationFolder, entry.name, existing, replace);
                if (destination.empty())
                {
                    status.done += entry.size;
                    if (sourcePath == item.path)
                    {
                        status.state = TransferState::Skipped; // the whole item was skipped
                    }
                    return true;
                }
                return CopyFile(item, sourcePath, destination, replace, status);
            }

            // A folder: merge into an existing folder of the same name, otherwise make it.
            std::string folder = JoinPath(destinationFolder, entry.name);
            std::set<std::string> inside;
            if (existing.count(entry.name) != 0)
            {
                FileEntry there;
                std::string ignored;
                if (item.to->Stat(folder, there, ignored) && !there.isFolder)
                {
                    status.error = folder + " exists and is not a folder";
                    return false;
                }
                inside = ListNames(*item.to, folder);
            }
            else if (!item.to->MakeFolder(folder, status.error))
            {
                return false;
            }
            std::vector<FileEntry> children;
            if (!item.from->List(sourcePath, children, status.error))
            {
                return false;
            }
            for (const FileEntry& child : children)
            {
                if (!CopyTree(item, JoinPath(sourcePath, child.name), child, folder, inside, status))
                {
                    return false;
                }
            }
            return true;
        }

        bool TransferQueue::CopyFile(const Item& item, const std::string& sourcePath,
                                     const std::string& destinationPath, bool replace, TransferStatus& status)
        {
            // A replacement goes to a temporary name first, so cancelling or a failure never costs the original.
            const std::string writePath = replace ? destinationPath + ".marconi-part" : destinationPath;
            std::unique_ptr<FileReader> reader = item.from->OpenRead(sourcePath, status.error);
            if (!reader)
            {
                return false;
            }
            // The destination was checked from its folder's listing, so there's no need to ask again here.
            std::unique_ptr<FileWriter> writer = item.to->OpenWrite(writePath, true, status.error);
            if (!writer)
            {
                return false;
            }
            std::vector<char> buffer(kChunk);
            auto lastReport = std::chrono::steady_clock::now();
            bool ok = true;
            for (;;)
            {
                if (m_cancel)
                {
                    ok = false;
                    break;
                }
                ptrdiff_t got = reader->Read(buffer.data(), buffer.size(), status.error);
                if (got < 0)
                {
                    ok = false;
                    break;
                }
                if (got == 0)
                {
                    break;
                }
                if (!writer->Write(buffer.data(), static_cast<size_t>(got), status.error))
                {
                    ok = false;
                    break;
                }
                status.done += static_cast<uint64_t>(got);
                auto now = std::chrono::steady_clock::now();
                if (now - lastReport >= kStatusInterval)
                {
                    lastReport = now;
                    m_status(status);
                }
            }
            std::string finishError;
            bool finished = writer->Finish(finishError);
            writer.reset();
            reader.reset();
            if (ok && !finished)
            {
                status.error = finishError;
                ok = false;
            }
            std::string ignored;
            if (!ok)
            {
                item.to->Remove(writePath, ignored); // never leave half a file behind
                return false;
            }
            if (replace)
            {
                if (!item.to->Remove(destinationPath, status.error) ||
                    !item.to->Rename(writePath, destinationPath, status.error))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace Files
} // namespace Marconi
