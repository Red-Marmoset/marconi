// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The delaying relay

#include "DelayProxy.h"

namespace MarconiServerTest
{
    bool DelayProxy::Start(uint16_t upstreamPort, std::string& error)
    {
        if (!m_listen.Listen("127.0.0.1", 0, error))
        {
            return false;
        }
        m_serve = std::thread([this, upstreamPort]() { Serve(upstreamPort); });
        return true;
    }

    void DelayProxy::Serve(uint16_t upstreamPort)
    {
        std::string error;
        if (!m_listen.Accept(m_client, 5000, error) || m_stopping)
        {
            return;
        }
        if (!m_upstream.Connect("127.0.0.1", upstreamPort, 5000, error, &m_stopping))
        {
            m_client.Shutdown();
            return;
        }
        m_toServer.from = &m_client;
        m_toServer.to = &m_upstream;
        m_toClient.from = &m_upstream;
        m_toClient.to = &m_client;
        StartDirection(m_toServer);
        StartDirection(m_toClient);
    }

    void DelayProxy::StartDirection(Direction& direction)
    {
        direction.reader = std::thread([this, &direction]() {
            std::vector<uint8_t> buffer(64 * 1024);
            while (!m_stopping)
            {
                ptrdiff_t n = direction.from->Receive(buffer.data(), buffer.size());
                if (n <= 0)
                {
                    break;
                }
                std::lock_guard<std::mutex> lock(direction.mutex);
                direction.queue.emplace_back(Clock::now() + m_delay,
                                             std::vector<uint8_t>(buffer.begin(), buffer.begin() + n));
                direction.changed.notify_all();
            }
            std::lock_guard<std::mutex> lock(direction.mutex);
            direction.closed = true;
            direction.changed.notify_all();
        });
        direction.writer = std::thread([this, &direction]() {
            std::unique_lock<std::mutex> lock(direction.mutex);
            while (!m_stopping)
            {
                if (direction.queue.empty())
                {
                    if (direction.closed)
                    {
                        break;
                    }
                    direction.changed.wait(lock);
                    continue;
                }
                Clock::time_point due = direction.queue.front().first;
                if (Clock::now() < due)
                {
                    direction.changed.wait_until(lock, due);
                    continue;
                }
                std::vector<uint8_t> chunk = std::move(direction.queue.front().second);
                direction.queue.pop_front();
                lock.unlock();
                bool sent = direction.to->SendAll(chunk.data(), chunk.size());
                lock.lock();
                if (!sent)
                {
                    break;
                }
            }
            // The other end of this direction has finished: pass the close on, as a real link would.
            direction.to->Shutdown();
        });
    }

    void DelayProxy::Stop()
    {
        m_stopping = true;
        m_listen.Shutdown();
        m_client.Shutdown();
        m_upstream.Shutdown();
        if (m_serve.joinable())
        {
            m_serve.join(); // it starts the two directions, so it finishes first
        }
        for (Direction* direction : {&m_toServer, &m_toClient})
        {
            {
                std::lock_guard<std::mutex> lock(direction->mutex);
                direction->closed = true;
                direction->changed.notify_all();
            }
            if (direction->reader.joinable())
            {
                direction->reader.join();
            }
            if (direction->writer.joinable())
            {
                direction->writer.join();
            }
        }
    }
} // namespace MarconiServerTest
