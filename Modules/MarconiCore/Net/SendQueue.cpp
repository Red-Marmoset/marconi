// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The sending thread

#include <MarconiCore/Net/SendQueue.h>

#include <chrono>
#include <utility>

namespace Marconi
{
    namespace Net
    {
        void SendQueue::Start()
        {
            std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_running)
                {
                    return;
                }
                m_jobs.clear();
                m_running = true;
            }
            m_thread = std::thread([this]() { Run(); });
        }

        void SendQueue::Post(std::function<void()> job, uint32_t replaceKey)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running)
                {
                    return;
                }
                if (replaceKey != 0 && !m_jobs.empty() && m_jobs.back().replaceKey == replaceKey)
                {
                    m_jobs.back().run = std::move(job);
                    return;
                }
                m_jobs.push_back(Job{std::move(job), replaceKey});
            }
            m_changed.notify_all();
        }

        bool SendQueue::WaitUntilSent(int timeoutMs)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                      [this]() { return !m_running || (m_jobs.empty() && !m_busy); });
        }

        void SendQueue::Stop(int flushMs)
        {
            std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);
            if (flushMs > 0)
            {
                WaitUntilSent(flushMs);
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_running = false;
                m_jobs.clear();
            }
            m_changed.notify_all();
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

        void SendQueue::Run()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            for (;;)
            {
                m_changed.wait(lock, [this]() { return !m_running || !m_jobs.empty(); });
                if (!m_running)
                {
                    return;
                }
                std::function<void()> job = std::move(m_jobs.front().run);
                m_jobs.pop_front();
                m_busy = true;
                lock.unlock();
                job();
                lock.lock();
                m_busy = false;
                m_changed.notify_all(); // for WaitUntilSent
            }
        }
    } // namespace Net
} // namespace Marconi
