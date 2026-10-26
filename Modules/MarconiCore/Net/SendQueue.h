// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A thread that sends a connection's messages in order, so the thread that queued them never waits on the network

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Marconi
{
    namespace Net
    {
        // The viewer's input, clipboard and requests go through one of these: Post() returns at once and the job
        // (which does the actual send) runs on the queue's own thread, after everything posted before it. A slow
        // or stalled link then holds up this thread, never the window.
        class SendQueue
        {
        public:
            SendQueue() = default;
            ~SendQueue() { Stop(); }

            SendQueue(const SendQueue&) = delete;
            SendQueue& operator=(const SendQueue&) = delete;

            // Starts the thread, with nothing queued. Does nothing if it is already running. Any thread.
            void Start();

            // Queues `job`. A non-zero `replaceKey` lets it take the place of the last job still waiting if that
            // one was posted with the same key, so a burst of pointer moves sends only the newest position. Ignored
            // while stopped. Any thread.
            void Post(std::function<void()> job, uint32_t replaceKey = 0);

            // Gives what's queued up to `flushMs` to go out, then drops the rest and waits for the job in progress.
            // The caller shuts the connection down first when it can't wait, so a send stuck on a slow link
            // returns. Any thread but the queue's own; safe to call more than once.
            void Stop(int flushMs = 0);

            // Waits up to `timeoutMs` for everything posted so far to be sent. True if it was. Any thread but the
            // queue's own.
            bool WaitUntilSent(int timeoutMs);

        private:
            struct Job
            {
                std::function<void()> run;
                uint32_t replaceKey = 0;
            };

            void Run();

            std::mutex m_lifecycleMutex; // held by Start and Stop, so two threads stopping can't both join
            std::thread m_thread;
            std::mutex m_mutex;
            std::condition_variable m_changed;
            std::deque<Job> m_jobs; // guarded by m_mutex
            bool m_running = false; // guarded by m_mutex
            bool m_busy = false;    // guarded by m_mutex: a job is being run
        };
    } // namespace Net
} // namespace Marconi
