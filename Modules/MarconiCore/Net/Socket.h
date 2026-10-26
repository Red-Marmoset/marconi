// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal TCP socket (client connect with timeout, plus listen/accept) whose waits can always be interrupted

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct addrinfo;

namespace Marconi
{
    namespace Net
    {
        class Socket
        {
        public:
            Socket();
            ~Socket();

            Socket(const Socket&) = delete;
            Socket& operator=(const Socket&) = delete;
            Socket(Socket&& other) noexcept;
            Socket& operator=(Socket&& other) noexcept;

            // Resolves `host` and connects to the first address that accepts within `timeoutMs`. Leaves the socket
            // non-blocking (Receive and SendAll wait for it) with TCP_NODELAY and TCP keep-alive set (so a dead link
            // is noticed within a minute or so). On failure `error` describes why. If `cancel` becomes true while
            // waiting, gives up promptly, even in the middle of looking the name up.
            bool Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error,
                         const std::atomic<bool>* cancel = nullptr);
            // As Connect, for numeric addresses tried in the given order (tests; resolving is skipped).
            bool ConnectAddresses(const std::vector<std::string>& numericHosts, uint16_t port, int timeoutMs,
                                  std::string& error, const std::atomic<bool>* cancel = nullptr);

            // Listens on `bindAddress`:`port` (port 0 picks a free one; see GetLocalPort).
            bool Listen(const std::string& bindAddress, uint16_t port, std::string& error);
            bool Accept(Socket& outClient, int timeoutMs, std::string& error);
            uint16_t GetLocalPort() const;

            // Waits until every byte is sent. Returns false if the connection failed or Shutdown() was called, which
            // wakes a send waiting for a slow receiver within ~100ms.
            bool SendAll(const void* data, size_t length);

            // Blocks until at least one byte arrives. Returns bytes read, 0 on orderly close or after Shutdown(),
            // -1 on error, including when the receive timeout passes with nothing arriving (TimedOut() then says so).
            ptrdiff_t Receive(void* buffer, size_t length);

            // How long one Receive may wait for data before giving up, or 0 to wait for ever (the default). Set while
            // the server is expected to answer promptly, such as during the handshake. Safe to call from any thread.
            void SetReceiveTimeout(int timeoutMs);
            bool TimedOut() const { return m_timedOut.load(); }

            // True when Receive won't block (data or end of stream is waiting), false after `timeoutMs`.
            bool WaitReadable(int timeoutMs) const;

            // Shuts down both directions and wakes any thread waiting in Receive or SendAll (within ~100ms; a blocked
            // recv() or send() isn't reliably woken by shutdown() on Windows, so both poll). Safe to call from
            // another thread.
            void Shutdown();
            void Close();
            bool IsOpen() const;

            // The OS socket, for a library that runs its own protocol over this connection (SSH). This object still
            // owns and closes it.
            intptr_t GetHandle() const { return m_handle; }

            static std::string GetLastErrorString();

        private:
            void ConnectFirst(const std::vector<const addrinfo*>& addresses, const std::string& target, int timeoutMs,
                              std::string& error, const std::atomic<bool>* cancel);

            intptr_t m_handle;
            std::atomic<bool> m_shutdown{false};
            std::atomic<int> m_receiveTimeoutMs{0};
            std::atomic<bool> m_timedOut{false};
        };
    } // namespace Net
} // namespace Marconi
