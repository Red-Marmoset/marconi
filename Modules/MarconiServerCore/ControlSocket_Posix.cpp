// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The control connection over a Unix domain socket (macOS and Linux); see ControlSocket.h. Written and run on Linux
// only: the macOS build has not been tried.

#include "ControlSocket.h"

#include <Log/Log.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <list>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            namespace
            {
                class FdTransport : public Wire::Transport
                {
                public:
                    explicit FdTransport(int fd) : m_fd(fd) {}
                    ~FdTransport() override
                    {
                        if (m_fd >= 0)
                        {
                            ::close(m_fd);
                        }
                    }

                    ptrdiff_t Read(void* buffer, size_t length) override
                    {
                        for (;;)
                        {
                            ssize_t got = ::recv(m_fd, buffer, length, 0);
                            if (got < 0 && errno == EINTR)
                            {
                                continue;
                            }
                            return got;
                        }
                    }

                    bool Write(const void* data, size_t length) override
                    {
                        const uint8_t* bytes = static_cast<const uint8_t*>(data);
                        while (length > 0)
                        {
                            int flags = 0;
#ifdef MSG_NOSIGNAL
                            flags = MSG_NOSIGNAL;
#endif
                            ssize_t sent = ::send(m_fd, bytes, length, flags);
                            if (sent < 0)
                            {
                                if (errno == EINTR)
                                {
                                    continue;
                                }
                                return false;
                            }
                            bytes += sent;
                            length -= static_cast<size_t>(sent);
                        }
                        return true;
                    }

                    void Shutdown() { ::shutdown(m_fd, SHUT_RDWR); }

                private:
                    int m_fd;
                };

                bool FillAddress(const std::string& path, sockaddr_un& address, std::string& error)
                {
                    std::memset(&address, 0, sizeof(address));
                    address.sun_family = AF_UNIX;
                    if (path.empty() || path.size() >= sizeof(address.sun_path))
                    {
                        error = "the control socket path is empty or too long";
                        return false;
                    }
                    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
                    return true;
                }

                bool PeerIsRoot(int fd)
                {
#if defined(__linux__)
                    ucred credentials{};
                    socklen_t size = sizeof(credentials);
                    return ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0 &&
                           credentials.uid == 0;
#else
                    uid_t user = 1;
                    gid_t group = 1;
                    return ::getpeereid(fd, &user, &group) == 0 && user == 0;
#endif
                }
            } // namespace

            struct ControlSocketServer::Impl
            {
                struct Connection
                {
                    Connection(int fd, ControlHost& host, const PeerInfo& peer, const std::string& version)
                        : transport(fd), session(transport, host, peer, version)
                    {
                    }
                    FdTransport transport;
                    ControlSession session;
                    std::thread thread;
                    std::atomic<bool> done{false};
                };

                Impl(ControlHost& h, const std::string& v, SocketAdmins a) : host(h), version(v), admins(a) {}

                void AcceptLoop()
                {
                    while (!stop)
                    {
                        pollfd waiting{listenFd, POLLIN, 0};
                        int ready = ::poll(&waiting, 1, 200);
                        Reap(false);
                        if (ready <= 0)
                        {
                            continue;
                        }
                        int fd = ::accept(listenFd, nullptr, nullptr);
                        if (fd < 0)
                        {
                            continue;
                        }
                        PeerInfo peer;
                        peer.isAdministrator = admins == SocketAdmins::Everybody ||
                                               (admins == SocketAdmins::RootOnly && PeerIsRoot(fd));
                        auto connection = std::make_unique<Connection>(fd, host, peer, version);
                        Connection* raw = connection.get();
                        std::lock_guard<std::mutex> lock(mutex);
                        connections.push_back(std::move(connection));
                        raw->thread = std::thread([raw]() {
                            raw->session.Run();
                            raw->done = true;
                        });
                    }
                }

                // Joins and drops the connections that have ended (all of them when `everything`, after shutting
                // them down).
                void Reap(bool everything)
                {
                    std::list<std::unique_ptr<Connection>> finished;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        for (auto it = connections.begin(); it != connections.end();)
                        {
                            if (everything)
                            {
                                (*it)->session.Stop();
                                (*it)->transport.Shutdown();
                            }
                            if (everything || (*it)->done)
                            {
                                finished.push_back(std::move(*it));
                                it = connections.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }
                    for (auto& connection : finished)
                    {
                        if (connection->thread.joinable())
                        {
                            connection->thread.join();
                        }
                    }
                }

                ControlHost& host;
                std::string version;
                SocketAdmins admins;
                int listenFd = -1;
                std::string path;
                std::atomic<bool> stop{false};
                std::thread acceptThread;
                std::mutex mutex;
                std::list<std::unique_ptr<Connection>> connections;
            };

            ControlSocketServer::ControlSocketServer(ControlHost& host, const std::string& serverVersion,
                                                     SocketAdmins admins)
                : m_impl(new Impl(host, serverVersion, admins))
            {
            }

            ControlSocketServer::~ControlSocketServer()
            {
                Stop();
            }

            bool ControlSocketServer::Start(const std::string& path, std::string& error)
            {
                if (m_impl->listenFd >= 0)
                {
                    error = "the control socket is already listening";
                    return false;
                }
                sockaddr_un address;
                if (!FillAddress(path, address, error))
                {
                    return false;
                }
                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0)
                {
                    error = std::string("could not make a socket: ") + std::strerror(errno);
                    return false;
                }
                ::unlink(path.c_str());
                const mode_t oldMask = ::umask(0177); // only this user may connect
                const int bound = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
                ::umask(oldMask);
                if (bound != 0 || ::listen(fd, 4) != 0)
                {
                    error = std::string("could not listen on ") + path + ": " + std::strerror(errno);
                    ::close(fd);
                    return false;
                }
                m_impl->listenFd = fd;
                m_impl->path = path;
                m_impl->stop = false;
                m_impl->acceptThread = std::thread([this]() { m_impl->AcceptLoop(); });
                return true;
            }

            void ControlSocketServer::Stop()
            {
                if (m_impl->listenFd < 0)
                {
                    return;
                }
                m_impl->stop = true;
                if (m_impl->acceptThread.joinable())
                {
                    m_impl->acceptThread.join();
                }
                m_impl->Reap(true);
                ::close(m_impl->listenFd);
                m_impl->listenFd = -1;
                ::unlink(m_impl->path.c_str());
            }

            void ControlSocketServer::NotifyChanged()
            {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                for (auto& connection : m_impl->connections)
                {
                    connection->session.PushStatus();
                }
            }

            std::unique_ptr<Wire::Transport> ConnectToControlSocket(const std::string& path, std::string& error)
            {
                sockaddr_un address;
                if (!FillAddress(path, address, error))
                {
                    return nullptr;
                }
                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0)
                {
                    error = std::string("could not make a socket: ") + std::strerror(errno);
                    return nullptr;
                }
                if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
                {
                    error = "the Marconi Server control socket is not there";
                    ::close(fd);
                    return nullptr;
                }
                return std::make_unique<FdTransport>(fd);
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
