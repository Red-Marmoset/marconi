// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal blocking TCP socket (client connect with timeout, plus listen/accept for tests)

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RmVnc
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

            // Resolves `host` and connects to the first address that accepts within `timeoutMs`.
            // Leaves the socket in blocking mode with TCP_NODELAY set. On failure `error` describes why.
            // If `cancel` becomes true while waiting, gives up promptly.
            bool Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error,
                         const std::atomic<bool>* cancel = nullptr);

            // Listens on `bindAddress`:`port` (port 0 picks a free one; see GetLocalPort).
            bool Listen(const std::string& bindAddress, uint16_t port, std::string& error);
            bool Accept(Socket& outClient, int timeoutMs, std::string& error);
            uint16_t GetLocalPort() const;

            // Blocks until every byte is sent. Returns false if the connection failed.
            bool SendAll(const void* data, size_t length);

            // Blocks until at least one byte arrives. Returns bytes read, 0 on orderly close or after Shutdown(),
            // -1 on error.
            ptrdiff_t Receive(void* buffer, size_t length);

            // Shuts down both directions and wakes any thread blocked in Receive (within ~100ms; a blocked recv()
            // isn't reliably woken by shutdown() on Windows, so Receive polls). Safe to call from another thread.
            void Shutdown();
            void Close();
            bool IsOpen() const;

            static std::string GetLastErrorString();

        private:
            intptr_t m_handle;
            std::atomic<bool> m_shutdown{false};
        };
    } // namespace Net
} // namespace RmVnc
