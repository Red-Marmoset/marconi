// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The connection acceptor

#include "Listener.h"

#include "SocketTransport.h"

#include <Log/Log.h>

#include <chrono>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        Listener::Listener(ScreenSource& screen, InputSink& input, const Settings& settings)
            : m_screen(screen), m_input(input), m_settings(settings)
        {
        }

        bool Listener::Start(std::string& error, const std::string& bindAddress)
        {
            if (!m_listenSocket.Listen(bindAddress, m_settings.port, error))
            {
                return false;
            }
            m_port = m_listenSocket.GetLocalPort();
            LOG_INFO("Server", "Listening on %s:%u", bindAddress.c_str(), static_cast<unsigned>(m_port));
            return true;
        }

        bool Listener::ServeOne(int acceptTimeoutMs)
        {
            if (m_stop)
            {
                return false;
            }
            Net::Socket client;
            std::string error;
            // 0 means wait for ever; Accept treats <=0 as its own kind of wait, so pass a large value instead.
            if (!m_listenSocket.Accept(client, acceptTimeoutMs > 0 ? acceptTimeoutMs : 24 * 60 * 60 * 1000, error))
            {
                return false; // timed out, was shut down, or failed; the caller (or Run) decides what next
            }

            SocketTransport transport(client);
            Session session(transport, m_screen, m_input, m_settings);
            session.SetFrameIntervalMs(m_frameIntervalMs);
            m_current = &session;
            session.Run();
            m_current = nullptr;
            client.Close();
            return true;
        }

        void Listener::Run()
        {
            // Serve viewers one after another. A failed Accept while stopping ends the loop; otherwise it waits a
            // moment so a transient error can't spin the CPU, then tries again.
            while (!m_stop)
            {
                if (!ServeOne(1000) && !m_stop)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }

        void Listener::Stop()
        {
            m_stop = true;
            Session* current = m_current.load();
            if (current != nullptr)
            {
                current->Stop();
            }
            m_listenSocket.Shutdown(); // wake a blocked Accept
        }
    } // namespace Server
} // namespace Marconi
