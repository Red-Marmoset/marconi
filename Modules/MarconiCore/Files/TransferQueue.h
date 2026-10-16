// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Copies files and whole folders between two file sources on a worker thread, with progress and cancelling

#pragma once

#include <MarconiCore/Files/FileSource.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace Marconi
{
    namespace Files
    {
        enum class Conflict
        {
            Replace,
            Skip,
            KeepBoth, // copy under a numbered name, "report (2).pdf"
        };

        struct ConflictAnswer
        {
            Conflict choice = Conflict::Skip;
            bool forTheRest = false; // use the same answer for the rest of this copy without asking
        };

        enum class TransferState
        {
            Waiting,
            Copying,
            Done,
            Skipped,
            Failed,
            Cancelled,
        };

        struct TransferStatus
        {
            int id = 0;
            std::string name;  // the file or folder being copied
            uint64_t done = 0; // bytes
            uint64_t total = 0;
            TransferState state = TransferState::Waiting;
            std::string error; // when Failed
        };

        class TransferQueue
        {
        public:
            // Both are called on the worker thread. The conflict function may block (to ask the user).
            using StatusFunction = std::function<void(const TransferStatus& status)>;
            using ConflictFunction = std::function<ConflictAnswer(const std::string& destinationPath)>;

            TransferQueue(StatusFunction status, ConflictFunction conflict);
            // Cancels whatever is left and waits for the worker. The file sources must outlive the queue.
            ~TransferQueue();
            TransferQueue(const TransferQueue&) = delete;
            TransferQueue& operator=(const TransferQueue&) = delete;

            // Copies each path (file or folder) from `from` into `destinationFolder` on `to`. One status id per path.
            std::vector<int> Add(FileSource& from, const std::vector<std::string>& paths, FileSource& to,
                                 const std::string& destinationFolder);
            // Stops the copy in progress and drops everything waiting.
            void Cancel();
            bool IsBusy() const;
            void WaitUntilIdle();

        private:
            struct Item
            {
                int id = 0;
                int batch = 0;
                FileSource* from = nullptr;
                std::string path;
                FileSource* to = nullptr;
                std::string destinationFolder;
            };

            void Run();
            void CopyItem(const Item& item);
            bool CopyTree(const Item& item, const std::string& sourcePath, const FileEntry& entry,
                          const std::string& destinationFolder, const std::set<std::string>& existing,
                          TransferStatus& status);
            bool CopyFile(const Item& item, const std::string& sourcePath, const std::string& destinationPath,
                          bool replace, TransferStatus& status);
            uint64_t MeasureTree(FileSource& source, const std::string& path, const FileEntry& entry);
            static std::set<std::string> ListNames(FileSource& source, const std::string& folder);
            // Where to write `name` in `folder` (whose names are `existing`), after asking about an existing file.
            // Empty means skip.
            std::string ResolveConflict(const Item& item, const std::string& folder, const std::string& name,
                                        const std::set<std::string>& existing, bool& outReplace);

            StatusFunction m_status;
            ConflictFunction m_conflict;

            mutable std::mutex m_mutex;
            std::condition_variable m_changed;
            std::deque<Item> m_waiting;
            bool m_working = false;
            bool m_stopping = false;
            int m_nextBatch = 1;
            std::atomic<bool> m_cancel{false};

            // The worker's own: the answer to reuse for the rest of a batch.
            int m_answerBatch = 0;
            ConflictAnswer m_batchAnswer;

            std::thread m_thread;
        };
    } // namespace Files
} // namespace Marconi
