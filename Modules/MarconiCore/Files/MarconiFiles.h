// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A Marconi server's files, through file messages on the open Marconi connection (no second login)

#pragma once

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Wire/WireFileMessages.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace Marconi
{
    namespace Files
    {
        // Safe to use from the file panel and the transfer queue at the same time: each call has its own request
        // id, so their answers don't mix. Reading keeps two ranges in flight, so a copy isn't slowed by the round
        // trip; writing sends ranges without waiting and learns of a failure on the next write or at Finish.
        class MarconiFiles : public FileSource
        {
        public:
            // Sends one framed message; false if the connection is gone.
            using SendFunction = std::function<bool(const Wire::Bytes& framed)>;

            explicit MarconiFiles(SendFunction send);
            ~MarconiFiles() override;

            // The connection thread hands over each file answer here (FileResult, FileOpened, FileData and
            // FileWriteFailed; the body is positioned after the type byte). Throws Wire::WireError if malformed.
            // Returns false for any other type.
            bool OnMessage(Wire::Type type, Wire::Reader& body);
            // The connection ended: everything waiting, and every later call, fails.
            void Close();
            // The connection is back (with file transfer).
            void Reopen();

            std::string GetHome() override;
            // The folder files dropped on the remote screen go to; the home folder if the server can't say.
            std::string GetDesktop();
            bool List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error) override;
            bool Stat(const std::string& path, FileEntry& outEntry, std::string& error) override;
            bool MakeFolder(const std::string& path, std::string& error) override;
            bool Rename(const std::string& from, const std::string& to, std::string& error) override;
            bool Remove(const std::string& path, std::string& error) override;
            bool RemoveFolder(const std::string& path, std::string& error) override;
            std::unique_ptr<FileReader> OpenRead(const std::string& path, std::string& error) override;
            std::unique_ptr<FileWriter> OpenWrite(const std::string& path, bool overwrite,
                                                  std::string& error) override;

            // Reading from `offset` on, to carry on an interrupted download.
            std::unique_ptr<FileReader> OpenReadAt(const std::string& path, uint64_t offset, std::string& error);
            // Carrying on an interrupted upload: keeps the first `offset` bytes of the file there (it must have at
            // least that many) and the writer's first byte goes at `offset`. Stat the file for how much it has.
            std::unique_ptr<FileWriter> OpenWriteResume(const std::string& path, uint64_t offset,
                                                        std::string& error);

        private:
            class ReaderImpl;
            class WriterImpl;

            // One request waiting for its answer.
            struct Slot
            {
                bool done = false;
                Wire::Type type = Wire::Type::FileResult;
                Wire::FileResult result;
                Wire::FileOpened opened;
                Wire::FileData data;
            };

            uint32_t NewId();
            std::shared_ptr<Slot> Expect(uint32_t id);
            void Forget(uint32_t id);
            // Waits for the answer to `id`; false (with `error`) if the connection ended or it took too long.
            bool Await(uint32_t id, const std::shared_ptr<Slot>& slot, std::string& error);
            bool SendFramed(const Wire::Bytes& framed, std::string& error);
            // Sends a FileRequest and waits for its FileResult; false (with `error`) if it failed.
            bool Call(Wire::FileRequest request, Wire::FileResult& outResult, std::string& error);
            bool OpenHandle(Wire::FileOpen::Mode mode, const std::string& path, uint64_t offset, uint32_t& outHandle,
                            uint64_t& outSize, std::string& error);
            void CloseHandle(uint32_t handle); // without waiting for an answer
            bool TakeWriteError(uint32_t handle, std::string& error);

            SendFunction m_send;
            std::mutex m_mutex;
            std::condition_variable m_changed;
            std::map<uint32_t, std::shared_ptr<Slot>> m_pending;
            std::map<uint32_t, std::string> m_writeErrors; // handle -> why a write failed
            std::atomic<uint32_t> m_nextId{1};
            bool m_closed = false;
            std::string m_home;
            std::string m_desktop;
        };
    } // namespace Files
} // namespace Marconi
