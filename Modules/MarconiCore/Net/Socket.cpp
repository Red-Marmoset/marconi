// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal TCP socket (client connect with timeout, plus listen/accept) whose waits can always be interrupted

#include <MarconiCore/Net/Socket.h>

#include "SocketPlatform.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Marconi
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
                // Non-blocking for good: Receive and SendAll wait in select() themselves, in short slices, so
                // Shutdown() from another thread always gets them out (a send blocked in the system can't be woken
                // reliably, on Windows least of all).
                Platform::SetNonBlocking(handle, true);
                int on = 1;
                setsockopt(Native(handle), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
                Platform::DisableSigPipe(handle);
                // A cable pulled or a server that vanished would otherwise go unnoticed until something is sent:
                // probe after 15 quiet seconds, every 5 seconds, and give up after 4 unanswered probes.
                Platform::EnableKeepAlive(handle, 15, 5, 4);
            }

            // getaddrinfo can't be interrupted and can take many seconds when name lookups are slow, so it runs on
            // a thread of its own. The caller waits for it in slices, so it can give up when cancelled; the thread
            // then finishes by itself and frees what it found.
            struct Lookup
            {
                std::mutex mutex;
                std::condition_variable finished;
                bool done = false;
                bool abandoned = false;
                int status = 0;
                addrinfo* results = nullptr;
            };

            // Returns getaddrinfo's status, or sets `cancelled` (and returns non-zero) when it gave up.
            int Resolve(const std::string& host, const std::string& service, const addrinfo& hints,
                        const std::atomic<bool>* cancel, addrinfo*& results, bool& cancelled)
            {
                results = nullptr;
                cancelled = false;
                std::shared_ptr<Lookup> lookup = std::make_shared<Lookup>();
                std::thread([lookup, host, service, hints]() {
                    addrinfo* found = nullptr;
                    int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
                    std::lock_guard<std::mutex> lock(lookup->mutex);
                    if (lookup->abandoned)
                    {
                        if (found != nullptr)
                        {
                            freeaddrinfo(found);
                        }
                        return;
                    }
                    lookup->status = status;
                    lookup->results = found;
                    lookup->done = true;
                    lookup->finished.notify_all();
                }).detach();

                std::unique_lock<std::mutex> lock(lookup->mutex);
                while (!lookup->done)
                {
                    if (cancel != nullptr && cancel->load())
                    {
                        cancelled = true;
                        lookup->abandoned = true;
                        return -1;
                    }
                    lookup->finished.wait_for(lock, std::chrono::milliseconds(100));
                }
                results = lookup->results;
                return lookup->status;
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

        Socket::Socket(Socket&& other) noexcept
            : m_handle(other.m_handle),
              m_shutdown(other.m_shutdown.load()),
              m_receiveTimeoutMs(other.m_receiveTimeoutMs.load()),
              m_timedOut(other.m_timedOut.load())
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
                m_receiveTimeoutMs = other.m_receiveTimeoutMs.load();
                m_timedOut = other.m_timedOut.load();
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
            bool cancelled = false;
            int status = Resolve(host, service, hints, cancel, results, cancelled);
            if (cancelled)
            {
                error = "Cancelled";
                return false;
            }
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
            std::vector<const addrinfo*> addresses;
            for (const addrinfo* address = results; address != nullptr; address = address->ai_next)
            {
                addresses.push_back(address);
            }
            error = "No addresses for '" + host + "'";
            ConnectFirst(addresses, host + ":" + service, timeoutMs, error, cancel);
            freeaddrinfo(results);
            return IsOpen();
        }

        bool Socket::ConnectAddresses(const std::vector<std::string>& numericHosts, uint16_t port, int timeoutMs,
                                      std::string& error, const std::atomic<bool>* cancel)
        {
            Close();
            addrinfo hints = {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_NUMERICHOST;
            std::string service = std::to_string(port);
            std::vector<addrinfo*> owned;
            std::vector<const addrinfo*> addresses;
            for (const std::string& host : numericHosts)
            {
                addrinfo* result = nullptr;
                if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) == 0 && result != nullptr)
                {
                    owned.push_back(result);
                    addresses.push_back(result);
                }
            }
            error = "No usable addresses";
            ConnectFirst(addresses, "port " + service, timeoutMs, error, cancel);
            for (addrinfo* result : owned)
            {
                freeaddrinfo(result);
            }
            return IsOpen();
        }

        void Socket::ConnectFirst(const std::vector<const addrinfo*>& addresses, const std::string& target,
                                  int timeoutMs, std::string& error, const std::atomic<bool>* cancel)
        {
            // "Happy Eyeballs" (RFC 8305): an address that never answers (often a host's firewalled IPv6 address,
            // listed first) mustn't hold up the others. Start the next attempt if the current ones haven't
            // connected within kAttemptDelay, alternating address families, and keep whichever connects first.
            using Clock = std::chrono::steady_clock;
            constexpr std::chrono::milliseconds kAttemptDelay(250);

            std::vector<const addrinfo*> ordered;
            {
                std::vector<const addrinfo*> first;
                std::vector<const addrinfo*> other;
                for (const addrinfo* address : addresses)
                {
                    (address->ai_family == addresses.front()->ai_family ? first : other).push_back(address);
                }
                for (size_t i = 0; i < first.size() || i < other.size(); ++i)
                {
                    if (i < first.size())
                    {
                        ordered.push_back(first[i]);
                    }
                    if (i < other.size())
                    {
                        ordered.push_back(other[i]);
                    }
                }
            }

            const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
            Clock::time_point nextAttempt = Clock::now();
            size_t next = 0;
            std::vector<intptr_t> pending;
            intptr_t winner = Platform::kInvalidSocket;
            bool timedOut = false;
            auto closePending = [&pending]() {
                for (intptr_t handle : pending)
                {
                    Platform::CloseSocket(handle);
                }
                pending.clear();
            };

            while (winner == Platform::kInvalidSocket)
            {
                if (cancel != nullptr && cancel->load())
                {
                    closePending();
                    error = "Cancelled";
                    return;
                }
                Clock::time_point now = Clock::now();
                if (next < ordered.size() && (now >= nextAttempt || pending.empty()))
                {
                    const addrinfo* address = ordered[next++];
                    nextAttempt = now + kAttemptDelay;
                    intptr_t handle =
                        static_cast<intptr_t>(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
                    if (handle == Platform::kInvalidSocket)
                    {
                        error = "socket() failed: " + Platform::ErrorString(Platform::LastError());
                        nextAttempt = now;
                        continue;
                    }
                    Platform::SetNonBlocking(handle, true);
                    if (connect(Native(handle), address->ai_addr, static_cast<socklen_t>(address->ai_addrlen)) == 0)
                    {
                        winner = handle;
                        break;
                    }
                    int connectError = Platform::LastError();
                    if (!Platform::IsConnectInProgress(connectError))
                    {
                        error = "Could not connect to " + target + ": " + Platform::ErrorString(connectError);
                        Platform::CloseSocket(handle);
                        nextAttempt = now;
                        continue;
                    }
                    pending.push_back(handle);
                }
                if (pending.empty())
                {
                    if (next >= ordered.size())
                    {
                        break; // every address failed; `error` says why the last one did
                    }
                    continue;
                }
                if (now >= deadline)
                {
                    timedOut = true;
                    break;
                }

                // Wait in short slices so a cancel request is noticed quickly.
                Clock::duration wait = std::min<Clock::duration>(std::chrono::milliseconds(100), deadline - now);
                if (next < ordered.size())
                {
                    wait = std::min<Clock::duration>(wait, std::max<Clock::duration>(nextAttempt - now, {}));
                }
                long long waitMs = std::max<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(wait).count(), 1);
                fd_set writeSet;
                fd_set errorSet;
                FD_ZERO(&writeSet);
                FD_ZERO(&errorSet);
                int width = 0;
                for (intptr_t handle : pending)
                {
                    FD_SET(Native(handle), &writeSet);
                    FD_SET(Native(handle), &errorSet);
                    width = std::max(width, Platform::SelectWidth(handle));
                }
                timeval timeout;
                timeout.tv_sec = static_cast<long>(waitMs / 1000);
                timeout.tv_usec = static_cast<long>((waitMs % 1000) * 1000);
                // Windows reports a failed non-blocking connect via the except set rather than the write set.
                if (select(width, nullptr, &writeSet, &errorSet, &timeout) <= 0)
                {
                    continue;
                }
                for (size_t i = 0; i < pending.size();)
                {
                    intptr_t handle = pending[i];
                    if (!FD_ISSET(Native(handle), &writeSet) && !FD_ISSET(Native(handle), &errorSet))
                    {
                        ++i;
                        continue;
                    }
                    int socketError = 0;
                    socklen_t length = sizeof(socketError);
                    getsockopt(Native(handle), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &length);
                    pending.erase(pending.begin() + static_cast<ptrdiff_t>(i));
                    if (socketError == 0 && FD_ISSET(Native(handle), &writeSet))
                    {
                        winner = handle;
                        break;
                    }
                    error = "Could not connect to " + target + ": " + Platform::ErrorString(socketError);
                    Platform::CloseSocket(handle);
                    nextAttempt = Clock::now(); // a refusal is an answer: try the next address straight away
                }
            }
            closePending();
            if (winner == Platform::kInvalidSocket)
            {
                if (timedOut)
                {
                    error = "Timed out connecting to " + target;
                }
                return;
            }
            ConfigureConnected(winner);
            m_handle = winner;
            error.clear();
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
                if (m_shutdown)
                {
                    return false;
                }
                int chunk = static_cast<int>(std::min<size_t>(length, INT_MAX));
                auto sent = send(Native(m_handle), bytes, chunk, Platform::SendFlags());
                if (sent < 0)
                {
                    int error = Platform::LastError();
                    if (Platform::IsInterrupted(error))
                    {
                        continue;
                    }
                    if (Platform::IsWouldBlock(error))
                    {
                        // The other end isn't taking data as fast as it comes: wait for room, in slices, so a
                        // Shutdown() from another thread is noticed.
                        if (WaitFor(m_handle, false, 100) < 0 && !Platform::IsInterrupted(Platform::LastError()))
                        {
                            return false;
                        }
                        continue;
                    }
                    return false;
                }
                bytes += sent;
                length -= static_cast<size_t>(sent);
            }
            return true;
        }

        bool Socket::WaitReadable(int timeoutMs) const
        {
            return IsOpen() && WaitFor(m_handle, true, timeoutMs) > 0;
        }

        ptrdiff_t Socket::Receive(void* buffer, size_t length)
        {
            using Clock = std::chrono::steady_clock;
            int chunk = static_cast<int>(std::min<size_t>(length, INT_MAX));
            const int timeoutMs = m_receiveTimeoutMs.load();
            const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
            for (;;)
            {
                if (m_shutdown)
                {
                    return 0;
                }
                int ready = WaitFor(m_handle, true, 100);
                if (ready == 0)
                {
                    if (timeoutMs > 0 && Clock::now() >= deadline)
                    {
                        m_timedOut = true;
                        return -1;
                    }
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
                if (received < 0 && (Platform::IsInterrupted(Platform::LastError()) ||
                                     Platform::IsWouldBlock(Platform::LastError())))
                {
                    continue; // woken without data after all (the socket is non-blocking)
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

        void Socket::SetReceiveTimeout(int timeoutMs)
        {
            m_receiveTimeoutMs = std::max(timeoutMs, 0);
            m_timedOut = false;
        }

        void Socket::Close()
        {
            if (IsOpen())
            {
                Platform::CloseSocket(m_handle);
                m_handle = Platform::kInvalidSocket;
            }
            m_shutdown = false;
            m_receiveTimeoutMs = 0;
            m_timedOut = false;
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
} // namespace Marconi
