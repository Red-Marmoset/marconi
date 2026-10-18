// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Runs file operations one at a time on a worker thread, so a slow folder listing never freezes the window

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace MarconiUI
{
    class BackgroundWork
    {
    public:
        BackgroundWork() : m_thread([this]() { Run(); }) {}
        ~BackgroundWork() { Stop(); }

        void Post(std::function<void()> job)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    return;
                }
                m_jobs.push_back(std::move(job));
            }
            m_changed.notify_all();
        }

        // Drops what's waiting and waits for the job in progress, e.g. before the file source changes.
        void Drain()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_jobs.clear();
            m_changed.wait(lock, [this]() { return !m_busy; });
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    return;
                }
                m_stopping = true;
                m_jobs.clear();
            }
            m_changed.notify_all();
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

    private:
        void Run()
        {
            for (;;)
            {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_changed.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
                    if (m_stopping)
                    {
                        return;
                    }
                    job = std::move(m_jobs.front());
                    m_jobs.pop_front();
                    m_busy = true;
                }
                job();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_busy = false;
                }
                m_changed.notify_all();
            }
        }

        std::mutex m_mutex;
        std::condition_variable m_changed;
        std::deque<std::function<void()>> m_jobs;
        bool m_busy = false;
        bool m_stopping = false;
        std::thread m_thread;
    };
} // namespace MarconiUI
