// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Answers a viewer's file messages: browsing, reading and writing this computer's files

#pragma once

#include "GuardedFiles.h"

#include <MarconiCore/Wire/WireFileMessages.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        // One per viewer. The session hands it each file message; it does the work on its own thread, so a slow disk
        // or a big copy never holds up the screen or the viewer's keys, and sends the answers in order.
        class FileService
        {
        public:
            // Sends one framed message to the viewer; false if the connection is gone.
            using SendFunction = std::function<bool(const Wire::Bytes& framed)>;

            // `enabled` false answers every request with "not allowed". `root` is the one folder shared, or empty
            // for the whole computer.
            FileService(bool enabled, const std::string& root, SendFunction send);
            ~FileService();

            FileService(const FileService&) = delete;
            FileService& operator=(const FileService&) = delete;

            // True if `type` is a message for the file service (and so was taken). Decodes on the caller's thread,
            // so a malformed message throws Wire::WireError there, like every other message. Blocks while a lot of
            // written data is waiting for the disk, which slows the viewer down instead of filling memory.
            bool Handle(Wire::Type type, Wire::Reader& body);

            // Finishes what's queued, closes every open file and stops the thread.
            void Stop();

            // Most files a viewer can have open at once, and how much written data may wait for the disk.
            static constexpr size_t kMaxOpenFiles = 32;
            static constexpr size_t kMaxQueuedBytes = 8 * 1024 * 1024;

        private:
            struct OpenFile
            {
                std::fstream stream;
                bool writing = false;
                uint64_t position = 0; // writing: where the next write must start
                bool failed = false;   // writing: an earlier write failed
                std::string error;
            };

            struct Job
            {
                std::function<void()> run;
                size_t weight = 0;
            };

            void Enqueue(std::function<void()> run, size_t weight);
            void Run();
            bool Send(const Wire::Bytes& framed);

            void DoRequest(const Wire::FileRequest& request);
            void DoOpen(const Wire::FileOpen& open);
            void DoRead(const Wire::FileRead& read);
            void DoWrite(const Wire::FileWrite& write);
            void DoClose(const Wire::FileClose& close);

            bool m_enabled;
            GuardedFiles m_files;
            SendFunction m_send;

            std::mutex m_mutex;
            std::condition_variable m_changed;
            std::deque<Job> m_queue;
            size_t m_queuedBytes = 0;
            bool m_stopping = false;
            std::thread m_thread;

            // Used only on the service's thread.
            std::map<uint32_t, std::unique_ptr<OpenFile>> m_open;
            uint32_t m_nextHandle = 1;
        };
    } // namespace Server
} // namespace Marconi
