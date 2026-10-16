// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal IPv4 UDP datagram socket (send to an address, receive with a timeout), for mDNS discovery

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Net
    {
        class UdpSocket
        {
        public:
            UdpSocket();
            ~UdpSocket();
            UdpSocket(const UdpSocket&) = delete;
            UdpSocket& operator=(const UdpSocket&) = delete;

            // Opens a socket on an ephemeral port of every interface.
            bool Open(std::string& error);
            // As Open, bound to a specific address and port (port 0 picks a free one).
            bool Bind(const std::string& ipv4, uint16_t port, std::string& error);
            uint16_t GetLocalPort() const;

            bool SendTo(const std::string& ipv4, uint16_t port, const void* data, size_t size);
            // Waits up to `timeoutMs`. Returns the datagram's size (filling `fromIpv4` and `fromPort` if given), 0 on
            // timeout, -1 on error.
            ptrdiff_t ReceiveFrom(void* buffer, size_t size, int timeoutMs, std::string* fromIpv4 = nullptr,
                                  uint16_t* fromPort = nullptr);
            void Close();

        private:
            intptr_t m_handle;
        };
    } // namespace Net
} // namespace Marconi
