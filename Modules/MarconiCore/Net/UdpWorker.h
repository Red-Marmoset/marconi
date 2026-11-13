// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A UDP socket with a worker thread: receives datagrams and calls back with each (and on every poll, so the owner
// can run timers), sends to a peer address that can be changed. Used by the datagram channel (Wire::DatagramSession).
//
// The receive loop waits on the socket for at most `pollMs` (select), so Stop() returns within that time and the
// socket is never closed under a blocked reader. IPv4 only, like Net::UdpSocket underneath it.

#pragma once

#include <MarconiCore/Net/UdpSocket.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace Net
    {
        // Resolves a host name or dotted address to an IPv4 address. False (with `error`) if it has none.
        bool ResolveIpv4(const std::string& host, std::string& ipv4, std::string& error);

        class UdpWorker
        {
        public:
            using ReceiveFn =
                std::function<void(const uint8_t* data, size_t size, const std::string& fromIpv4, uint16_t fromPort)>;
            using PollFn = std::function<void()>;

            UdpWorker();
            ~UdpWorker();
            UdpWorker(const UdpWorker&) = delete;
            UdpWorker& operator=(const UdpWorker&) = delete;

            // Binds to `ipv4`:`port` (port 0 picks a free one).
            bool Bind(const std::string& ipv4, uint16_t port, std::string& error);
            uint16_t GetLocalPort() const { return m_socket.GetLocalPort(); }

            void SetPeer(const std::string& ipv4, uint16_t port);
            bool HasPeer() const;
            // Sends to the peer; false if there is none or the system refused it.
            bool SendToPeer(const void* data, size_t size);

            // Starts the thread. `onPoll` runs after every receive and every timeout (at least every pollMs).
            void Start(ReceiveFn onReceive, PollFn onPoll, int pollMs = 10);
            // Stops the thread and waits for it, then closes the socket. Safe to call twice.
            void Stop();

        private:
            void Run(int pollMs);
            void StopThread();

            UdpSocket m_socket;
            mutable std::mutex m_peerMutex;
            std::string m_peerIp;
            uint16_t m_peerPort = 0;
            ReceiveFn m_onReceive;
            PollFn m_onPoll;
            std::atomic<bool> m_stop{false};
            std::thread m_thread;
        };
    } // namespace Net
} // namespace Marconi
