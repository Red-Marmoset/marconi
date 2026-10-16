// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A remote computer's files over SFTP (SSH, e.g. a Mac with Remote Login on), for the file panel's right side

#pragma once

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Net/Socket.h>

#include <Crypto/Ssh.h>

#include <atomic>
#include <mutex>

namespace Marconi
{
    namespace Files
    {
        class SftpFiles : public FileSource
        {
        public:
            ~SftpFiles() override;

            // Connects and runs the SSH handshake. Check GetHostKey() against the trusted one before Login.
            bool Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error,
                         const std::atomic<bool>* cancel = nullptr);
            const Crypto::SshHostKey& GetHostKey() const;
            bool Login(const std::string& user, const std::string& password, std::string& error);
            bool IsLoggedIn() const { return m_loggedIn; }
            // From any thread: makes the call in progress (and every later one) fail promptly.
            void Abort();

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

        private:
            bool Ready(std::string& error) const;

            // libssh2 sessions are single-threaded: the panel and the transfer queue take turns.
            std::recursive_mutex m_mutex;
            Net::Socket m_socket;
            Crypto::SshSession m_session;
            std::atomic<bool> m_loggedIn{false};
            std::string m_home;
        };
    } // namespace Files
} // namespace Marconi
