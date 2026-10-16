// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A TightVNC server's files, through file transfer messages on the open VNC connection (no second login)

#pragma once

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Rfb/TightFileTransfer.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>

namespace Marconi
{
    namespace Files
    {
        class TightFiles : public FileSource
        {
        public:
            // Sends a request on the VNC connection; false if the connection is gone.
            using SendFunction = std::function<bool(const Rfb::Bytes& request)>;

            explicit TightFiles(SendFunction send);

            // The connection thread hands over each file transfer reply here.
            void OnReply(const Rfb::FileTransfer::Reply& reply);
            // The VNC connection ended: everything waiting, and every later call, fails.
            void Close();

            std::string GetHome() override;
            bool List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error) override;
            bool Stat(const std::string& path, FileEntry& outEntry, std::string& error) override;
            bool MakeFolder(const std::string& path, std::string& error) override;
            bool Rename(const std::string& from, const std::string& to, std::string& error) override;
            bool Remove(const std::string& path, std::string& error) override;
            bool RemoveFolder(const std::string& path, std::string& error) override;
            std::unique_ptr<FileReader> OpenRead(const std::string& path, std::string& error) override;
            std::unique_ptr<FileWriter> OpenWrite(const std::string& path, bool overwrite,
                                                  std::string& error) override;

            // Sends `request` and waits for the reply; a "request failed" reply becomes `error`. One at a time.
            bool Request(const Rfb::Bytes& request, Rfb::FileTransfer::Reply& outReply, std::string& error);

        private:
            SendFunction m_send;
            std::mutex m_requestMutex; // the server answers requests in order, one at a time
            std::mutex m_replyMutex;
            std::condition_variable m_replied;
            std::optional<Rfb::FileTransfer::Reply> m_reply;
            bool m_closed = false;
        };
    } // namespace Files
} // namespace Marconi
