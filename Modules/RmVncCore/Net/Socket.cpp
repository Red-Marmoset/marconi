// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal blocking TCP socket (client connect with timeout, plus listen/accept for tests)

#include <RmVncCore/Net/Socket.h>

#include "SocketPlatform.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace RmVnc
{
    namespace Net
    {
        namespace
        {
#if defined(_WIN32)
            using NativeHandle = SOCKET;
#else
            using NativeHandle = int;
#endif

            NativeHandle Native(intptr_t handle)
            {
                return static_cast<NativeHandle>(handle);
            }

            // Waits for the socket to become readable (read == true) or writable. Returns >0 ready, 0 timeout, <0 error.
            int WaitFor(intptr_t handle, bool read, int timeoutMs)
            {
                fd_set set;
                FD_ZERO(&set);
                FD_SET(Native(handle), &set);
                fd_set errorSet;
                FD_ZERO(&errorSet);
                FD_SET(Native(handle), &errorSet);
                timeval timeout;
                timeout.tv_sec = timeoutMs / 1000;
                timeout.tv_usec = (timeoutMs % 1000) * 1000;
                // Windows reports a failed non-blocking connect via the except set rather than the write set.
                return select(Platform::SelectWidth(handle), read ? &set : nullptr, read ? nullptr : &set, &errorSet,
                              timeoutMs < 0 ? nullptr : &timeout);
            }

            void ConfigureConnected(intptr_t handle)
            {
                int on = 1;
                setsockopt(Native(handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
                Platform::DisableSigPipe(handle);
            }
        } // namespace

        Socket::Socket() : m_handle(Platform::kInvalidSocket)
        {
            Platform::EnsureInitialised();
        }

        Socket::~Socket()
        {
            Close();
        }

        Socket::Socket(Socket&& other) noexcept : m_handle(other.m_handle), m_shutdown(other.m_shutdown.load())
        {
            other.m_handle = Platform::kInvalidSocket;
        }

        Socket& Socket::operator=(Socket&& other) noexcept
        {
            if (this != &other)
            {
                Close();
                m_handle = other.m_handle;
                m_shutdown = other.m_shutdown.load();
                other.m_handle = Platform::kInvalidSocket;
            }
            return *this;
        }

        bool Socket::Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error,
                             const std::atomic<bool>* cancel)
        {
            Close();

            addrinfo hints = {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            addrinfo* results = nullptr;
            std::string service = std::to_string(port);
            int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
            if (status != 0 || results == nullptr)
            {
                error = "Could not resolve '" + host + "'";
#if !defined(_WIN32)
                error += std::string(": ") + gai_strerror(status);
#else
                error += ": " + Platform::ErrorString(status);
#endif
                return false;
            }

            error = "No addresses for '" + host + "'";
            for (addrinfo* address = results; address != nullptr; address = address->ai_next)
            {
                intptr_t handle = static_cast<intptr_t>(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
                if (handle == Platform::kInvalidSocket)
                {
                    error = "socket() failed: " + Platform::ErrorString(Platform::LastError());
                    continue;
                }

                Platform::SetNonBlocking(handle, true);
                bool connected = connect(Native(handle), address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == 0;
                if (!connected)
                {
                    int connectError = Platform::LastError();
                    if (Platform::IsConnectInProgress(connectError))
                    {
                        // Wait in short slices so a cancel request is noticed quickly.
                        int ready = 0;
                        for (int waited = 0; ready == 0 && waited < timeoutMs; waited += 100)
                        {
                            if (cancel != nullptr && cancel->load())
                            {
                                break;
                            }
                            ready = WaitFor(handle, false, std::min(100, timeoutMs - waited));
                        }
                        if (cancel != nullptr && cancel->load())
                        {
                            Platform::CloseSocket(handle);
                            error = "Cancelled";
                            break;
                        }
                        if (ready > 0)
                        {
                            int socketError = 0;
                            socklen_t length = sizeof(socketError);
                            getsockopt(Native(handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &length);
                            connected = socketError == 0;
                            if (!connected)
                            {
                                error = "Could not connect to " + host + ":" + service + ": " + Platform::ErrorString(socketError);
                            }
                        }
                        else
                        {
                            error = ready == 0 ? "Timed out connecting to " + host + ":" + service
                                               : "Could not connect to " + host + ":" + service + ": " +
                                                     Platform::ErrorString(Platform::LastError());
                        }
                    }
                    else
                    {
                        error = "Could not connect to " + host + ":" + service + ": " + Platform::ErrorString(connectError);
                    }
                }

                if (connected)
                {
                    Platform::SetNonBlocking(handle, false);
                    ConfigureConnected(handle);
                    m_handle = handle;
                    error.clear();
                    break;
                }
                Platform::CloseSocket(handle);
            }
            freeaddrinfo(results);
            return IsOpen();
        }

        bool Socket::Listen(const std::string& bindAddress, uint16_t port, std::string& error)
        {
            Close();
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1)
            {
                error = "Bad bind address '" + bindAddress + "'";
                return false;
            }

            intptr_t handle = static_cast<intptr_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
            if (handle == Platform::kInvalidSocket)
            {
                error = "socket() failed: " + Platform::ErrorString(Platform::LastError());
                return false;
            }
            int on = 1;
            setsockopt(Native(handle), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
            if (bind(Native(handle), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
                listen(Native(handle), 4) != 0)
            {
                error = "bind/listen failed: " + Platform::ErrorString(Platform::LastError());
                Platform::CloseSocket(handle);
                return false;
            }
            m_handle = handle;
            return true;
        }

        bool Socket::Accept(Socket& outClient, int timeoutMs, std::string& error)
        {
            int ready = WaitFor(m_handle, true, timeoutMs);
            if (ready <= 0)
            {
                error = ready == 0 ? "Timed out waiting for a connection" : Platform::ErrorString(Platform::LastError());
                return false;
            }
            intptr_t handle = static_cast<intptr_t>(accept(Native(m_handle), nullptr, nullptr));
            if (handle == Platform::kInvalidSocket)
            {
                error = "accept() failed: " + Platform::ErrorString(Platform::LastError());
                return false;
            }
            ConfigureConnected(handle);
            outClient.Close();
            outClient.m_handle = handle;
            return true;
        }

        uint16_t Socket::GetLocalPort() const
        {
            sockaddr_storage address = {};
            socklen_t length = sizeof(address);
            if (getsockname(Native(m_handle), reinterpret_cast<sockaddr*>(&address), &length) != 0)
            {
                return 0;
            }
            if (address.ss_family == AF_INET)
            {
                return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
            }
            if (address.ss_family == AF_INET6)
            {
                return ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);
            }
            return 0;
        }

        bool Socket::SendAll(const void* data, size_t length)
        {
            const char* bytes = static_cast<const char*>(data);
            while (length > 0)
            {
                int chunk = static_cast<int>(std::min<size_t>(length, INT_MAX));
                auto sent = send(Native(m_handle), bytes, chunk, Platform::SendFlags());
                if (sent < 0)
                {
                    if (Platform::IsInterrupted(Platform::LastError()))
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

        ptrdiff_t Socket::Receive(void* buffer, size_t length)
        {
            int chunk = static_cast<int>(std::min<size_t>(length, INT_MAX));
            for (;;)
            {
                if (m_shutdown)
                {
                    return 0;
                }
                int ready = WaitFor(m_handle, true, 100);
                if (ready == 0)
                {
                    continue;
                }
                if (ready < 0)
                {
                    if (Platform::IsInterrupted(Platform::LastError()))
                    {
                        continue;
                    }
                    return -1;
                }

                auto received = recv(Native(m_handle), static_cast<char*>(buffer), chunk, 0);
                if (received < 0 && Platform::IsInterrupted(Platform::LastError()))
                {
                    continue;
                }
                return received < 0 ? -1 : static_cast<ptrdiff_t>(received);
            }
        }

        void Socket::Shutdown()
        {
            m_shutdown = true;
            if (IsOpen())
            {
                Platform::ShutdownSocket(m_handle);
            }
        }

        void Socket::Close()
        {
            if (IsOpen())
            {
                Platform::CloseSocket(m_handle);
                m_handle = Platform::kInvalidSocket;
            }
            m_shutdown = false;
        }

        bool Socket::IsOpen() const
        {
            return m_handle != Platform::kInvalidSocket;
        }

        std::string Socket::GetLastErrorString()
        {
            return Platform::ErrorString(Platform::LastError());
        }
    } // namespace Net
} // namespace RmVnc
