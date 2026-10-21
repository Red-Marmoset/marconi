// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A loopback relay that holds every chunk for a set time in each direction: a slow network for tests

#pragma once

#include <MarconiCore/Net/Socket.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MarconiServerTest
{
    // Listens on a free loopback port and relays one connection to `upstreamPort`, delivering each chunk read from
    // either side `oneWayDelayMs` after it arrived, in order. The bytes pass through untouched (TLS included), so
    // the real client and server run over it as over a link with that much delay each way.
    class DelayProxy
    {
    public:
        explicit DelayProxy(int oneWayDelayMs) : m_delay(std::chrono::milliseconds(oneWayDelayMs)) {}
        ~DelayProxy() { Stop(); }

        DelayProxy(const DelayProxy&) = delete;
        DelayProxy& operator=(const DelayProxy&) = delete;

        bool Start(uint16_t upstreamPort, std::string& error);
        uint16_t GetPort() const { return m_listen.GetLocalPort(); }
        void Stop();

    private:
        using Clock = std::chrono::steady_clock;

        // One direction of the relay: a reader thread queues chunks with the time they may go, a writer thread
        // sends each when its time comes.
        struct Direction
        {
            Marconi::Net::Socket* from = nullptr;
            Marconi::Net::Socket* to = nullptr;
            std::mutex mutex;
            std::condition_variable changed;
            std::deque<std::pair<Clock::time_point, std::vector<uint8_t>>> queue;
            bool closed = false;
            std::thread reader;
            std::thread writer;
        };

        void Serve(uint16_t upstreamPort);
        void StartDirection(Direction& direction);

        Clock::duration m_delay;
        Marconi::Net::Socket m_listen;
        Marconi::Net::Socket m_client;
        Marconi::Net::Socket m_upstream;
        Direction m_toServer;
        Direction m_toClient;
        std::thread m_serve;
        std::atomic<bool> m_stopping{false};
    };
} // namespace MarconiServerTest
