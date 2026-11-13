// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A UDP socket with a receive thread (see UdpWorker.h)

#include <MarconiCore/Net/UdpWorker.h>

#include "SocketPlatform.h"

#include <chrono>
#include <vector>

namespace Marconi
{
    namespace Net
    {
        bool ResolveIpv4(const std::string& host, std::string& ipv4, std::string& error)
        {
            Platform::EnsureInitialised();
            addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo* result = nullptr;
            int code = getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (code != 0 || result == nullptr)
            {
                error = "Could not find an IPv4 address for '" + host + "'";
                return false;
            }
            char text[INET_ADDRSTRLEN] = {};
            const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
            inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text));
            freeaddrinfo(result);
            ipv4 = text;
            return true;
        }

        UdpWorker::UdpWorker() = default;

        UdpWorker::~UdpWorker()
        {
            Stop();
        }

        bool UdpWorker::Bind(const std::string& ipv4, uint16_t port, std::string& error)
        {
            return m_socket.Bind(ipv4, port, error);
        }

        void UdpWorker::SetPeer(const std::string& ipv4, uint16_t port)
        {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            m_peerIp = ipv4;
            m_peerPort = port;
        }

        bool UdpWorker::HasPeer() const
        {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            return m_peerPort != 0;
        }

        bool UdpWorker::SendToPeer(const void* data, size_t size)
        {
            std::string ip;
            uint16_t port;
            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                ip = m_peerIp;
                port = m_peerPort;
            }
            return port != 0 && m_socket.SendTo(ip, port, data, size);
        }

        void UdpWorker::Start(ReceiveFn onReceive, PollFn onPoll, int pollMs)
        {
            StopThread();
            m_onReceive = std::move(onReceive);
            m_onPoll = std::move(onPoll);
            m_stop = false;
            m_thread = std::thread([this, pollMs]() { Run(pollMs); });
        }

        void UdpWorker::StopThread()
        {
            m_stop = true;
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        void UdpWorker::Stop()
        {
            StopThread();
            m_socket.Close();
        }

        void UdpWorker::Run(int pollMs)
        {
            std::vector<uint8_t> buffer(65536);
            while (!m_stop)
            {
                std::string fromIp;
                uint16_t fromPort = 0;
                ptrdiff_t got = m_socket.ReceiveFrom(buffer.data(), buffer.size(), pollMs, &fromIp, &fromPort);
                if (got > 0 && m_onReceive)
                {
                    m_onReceive(buffer.data(), static_cast<size_t>(got), fromIp, fromPort);
                }
                else if (got < 0)
                {
                    // An error (on Windows an ICMP "port unreachable" from an earlier send shows up here): don't spin.
                    std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
                }
                if (m_onPoll)
                {
                    m_onPoll();
                }
            }
        }
    } // namespace Net
} // namespace Marconi
