// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A remote computer's files over SFTP (SSH, e.g. a Mac with Remote Login on), for the file panel's right side

#include <MarconiCore/Files/SftpFiles.h>

namespace Marconi
{
    namespace Files
    {
        namespace
        {
            FileEntry ToEntry(const Crypto::SftpEntry& sftp)
            {
                FileEntry entry;
                entry.name = sftp.name;
                entry.size = sftp.size;
                entry.modified = sftp.modified;
                entry.isFolder = sftp.isFolder;
                return entry;
            }

            class SftpReader : public FileReader
            {
            public:
                SftpReader(std::recursive_mutex& mutex, std::unique_ptr<Crypto::SftpFile> file)
                    : m_mutex(mutex),
                      m_file(std::move(file))
                {
                }
                ~SftpReader() override
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    m_file.reset();
                }

                ptrdiff_t Read(void* buffer, size_t size, std::string& error) override
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    return m_file->Read(buffer, size, error);
                }

            private:
                std::recursive_mutex& m_mutex;
                std::unique_ptr<Crypto::SftpFile> m_file;
            };

            class SftpWriter : public FileWriter
            {
            public:
                SftpWriter(std::recursive_mutex& mutex, std::unique_ptr<Crypto::SftpFile> file)
                    : m_mutex(mutex),
                      m_file(std::move(file))
                {
                }
                ~SftpWriter() override
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    m_file.reset();
                }

                bool Write(const void* data, size_t size, std::string& error) override
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    return m_file->Write(data, size, error);
                }

                bool Finish(std::string& error) override
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    return m_file->Close(error);
                }

            private:
                std::recursive_mutex& m_mutex;
                std::unique_ptr<Crypto::SftpFile> m_file;
            };
        } // namespace

        SftpFiles::~SftpFiles()
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_session.Disconnect();
            m_socket.Close();
        }

        bool SftpFiles::Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error,
                                const std::atomic<bool>* cancel)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!m_socket.Connect(host, port, timeoutMs, error, cancel))
            {
                return false;
            }
            if (!m_session.Handshake(m_socket.GetHandle(), timeoutMs, error))
            {
                m_socket.Close();
                return false;
            }
            return true;
        }

        const Crypto::SshHostKey& SftpFiles::GetHostKey() const
        {
            return m_session.GetHostKey();
        }

        bool SftpFiles::Login(const std::string& user, const std::string& password, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!m_socket.IsOpen())
            {
                error = "Not connected";
                return false;
            }
            if (!m_session.Login(user, password, error))
            {
                return false;
            }
            if (!m_session.RealPath(".", m_home, error))
            {
                return false;
            }
            m_loggedIn = true;
            return true;
        }

        void SftpFiles::Abort()
        {
            m_loggedIn = false;
            m_socket.Shutdown(); // deliberately without the lock: it wakes the call holding it
        }

        bool SftpFiles::Ready(std::string& error) const
        {
            if (!m_loggedIn)
            {
                error = "Not connected";
                return false;
            }
            return true;
        }

        std::string SftpFiles::GetHome()
        {
            return m_home.empty() ? "/" : m_home;
        }

        bool SftpFiles::List(const std::string& folder, std::vector<FileEntry>& outEntries, std::string& error)
        {
            outEntries.clear();
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            std::vector<Crypto::SftpEntry> entries;
            if (!Ready(error) || !m_session.List(folder, entries, error))
            {
                return false;
            }
            for (const Crypto::SftpEntry& entry : entries)
            {
                outEntries.push_back(ToEntry(entry));
            }
            return true;
        }

        bool SftpFiles::Stat(const std::string& path, FileEntry& outEntry, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            Crypto::SftpEntry entry;
            if (!Ready(error) || !m_session.Stat(path, entry, error))
            {
                return false;
            }
            outEntry = ToEntry(entry);
            return true;
        }

        bool SftpFiles::MakeFolder(const std::string& path, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return Ready(error) && m_session.MakeFolder(path, error);
        }

        bool SftpFiles::Rename(const std::string& from, const std::string& to, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return Ready(error) && m_session.Rename(from, to, error);
        }

        bool SftpFiles::Remove(const std::string& path, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return Ready(error) && m_session.Remove(path, error);
        }

        bool SftpFiles::RemoveFolder(const std::string& path, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return Ready(error) && m_session.RemoveFolder(path, error);
        }

        std::unique_ptr<FileReader> SftpFiles::OpenRead(const std::string& path, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!Ready(error))
            {
                return nullptr;
            }
            std::unique_ptr<Crypto::SftpFile> file = m_session.OpenRead(path, error);
            return file ? std::make_unique<SftpReader>(m_mutex, std::move(file)) : nullptr;
        }

        std::unique_ptr<FileWriter> SftpFiles::OpenWrite(const std::string& path, bool overwrite, std::string& error)
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!Ready(error))
            {
                return nullptr;
            }
            std::unique_ptr<Crypto::SftpFile> file = m_session.OpenWrite(path, overwrite, error);
            return file ? std::make_unique<SftpWriter>(m_mutex, std::move(file)) : nullptr;
        }
    } // namespace Files
} // namespace Marconi
