// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Minimal IPv4 UDP datagram socket (send to an address, receive with a timeout), for mDNS discovery

#include <MarconiCore/Net/UdpSocket.h>

#include "SocketPlatform.h"

#include <algorithm>
#include <climits>

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
        } // namespace

        UdpSocket::UdpSocket() : m_handle(Platform::kInvalidSocket)
        {
            Platform::EnsureInitialised();
        }

        UdpSocket::~UdpSocket()
        {
            Close();
        }

        bool UdpSocket::Open(std::string& error)
        {
            return Bind("0.0.0.0", 0, error);
        }

        bool UdpSocket::Bind(const std::string& ipv4, uint16_t port, std::string& error)
        {
            Close();
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1)
            {
                error = "Bad bind address '" + ipv4 + "'";
                return false;
            }
            intptr_t handle = static_cast<intptr_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            if (handle == Platform::kInvalidSocket)
            {
                error = "socket() failed: " + Platform::ErrorString(Platform::LastError());
                return false;
            }
            if (bind(Native(handle), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            {
                error = "bind failed: " + Platform::ErrorString(Platform::LastError());
                Platform::CloseSocket(handle);
                return false;
            }
            m_handle = handle;
            return true;
        }

        uint16_t UdpSocket::GetLocalPort() const
        {
            sockaddr_in address = {};
            socklen_t length = sizeof(address);
            if (m_handle == Platform::kInvalidSocket ||
                getsockname(Native(m_handle), reinterpret_cast<sockaddr*>(&address), &length) != 0)
            {
                return 0;
            }
            return ntohs(address.sin_port);
        }

        bool UdpSocket::SendTo(const std::string& ipv4, uint16_t port, const void* data, size_t size)
        {
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            if (m_handle == Platform::kInvalidSocket || inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1)
            {
                return false;
            }
            int length = static_cast<int>(std::min<size_t>(size, INT_MAX));
            return sendto(Native(m_handle), static_cast<const char*>(data), length, 0,
                          reinterpret_cast<sockaddr*>(&address), sizeof(address)) == length;
        }

        ptrdiff_t UdpSocket::ReceiveFrom(void* buffer, size_t size, int timeoutMs, std::string* fromIpv4,
                                         uint16_t* fromPort)
        {
            if (m_handle == Platform::kInvalidSocket)
            {
                return -1;
            }
            fd_set set;
            FD_ZERO(&set);
            FD_SET(Native(m_handle), &set);
            timeval timeout;
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            int ready = select(Platform::SelectWidth(m_handle), &set, nullptr, nullptr, &timeout);
            if (ready <= 0)
            {
                return ready == 0 ? 0 : -1;
            }
            sockaddr_in from = {};
            socklen_t fromLength = sizeof(from);
            int length = static_cast<int>(std::min<size_t>(size, INT_MAX));
            int got = recvfrom(Native(m_handle), static_cast<char*>(buffer), length, 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (got < 0)
            {
                return -1;
            }
            if (fromIpv4 != nullptr)
            {
                char text[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
                *fromIpv4 = text;
            }
            if (fromPort != nullptr)
            {
                *fromPort = ntohs(from.sin_port);
            }
            return got;
        }

        void UdpSocket::Close()
        {
            if (m_handle != Platform::kInvalidSocket)
            {
                Platform::CloseSocket(m_handle);
                m_handle = Platform::kInvalidSocket;
            }
        }
    } // namespace Net
} // namespace Marconi
