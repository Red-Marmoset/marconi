// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A connection through SSH: log in to the other computer's SSH server and have it connect to the VNC server for us

#include <MarconiCore/Net/SshTunnel.h>

#include <Log/Log.h>

namespace Marconi
{
    namespace Net
    {
        SshTunnel::SshTunnel() = default;

        SshTunnel::~SshTunnel()
        {
            Shutdown();
            m_channel.reset(); // before the session and the socket it runs on
            m_session.Disconnect();
            m_socket.Close();
        }

        bool SshTunnel::Connect(const SshTunnelOptions& options, std::string& error, bool& refused,
                                const std::atomic<bool>* cancel)
        {
            refused = false;
            if (!m_socket.Connect(options.host, options.port, options.timeoutMs, error, cancel))
            {
                error = "Couldn't reach the SSH server on " + options.host + ": " + error;
                return false;
            }
            if (!m_session.Handshake(m_socket.GetHandle(), options.timeoutMs, error))
            {
                m_socket.Close();
                return false;
            }
            const Crypto::SshHostKey& key = m_session.GetHostKey();
            m_fingerprint = key.fingerprint;
            if (m_fingerprint != options.trustedFingerprint)
            {
                SshHostKeyQuestion question;
                question.host = options.host;
                question.algorithm = key.algorithm;
                question.fingerprint = key.fingerprint;
                question.previous = options.trustedFingerprint;
                if (!options.trustedFingerprint.empty())
                {
                    LOG_WARNING("Ssh", "The SSH host key for %s changed: was %s, now %s", options.host.c_str(),
                                options.trustedFingerprint.c_str(), key.fingerprint.c_str());
                }
                if (!options.trustDecision || !options.trustDecision(question))
                {
                    refused = true;
                    error = "The SSH server's key was not accepted";
                    m_session.Disconnect();
                    m_socket.Close();
                    return false;
                }
            }
            return true;
        }

        bool SshTunnel::Login(const std::string& user, const std::string& password, std::string& error)
        {
            return m_session.Login(user, password, error);
        }

        bool SshTunnel::Open(const std::string& host, uint16_t port, std::string& error)
        {
            m_channel = m_session.OpenTunnel(host, port, error);
            if (m_channel && m_shutDown)
            {
                m_channel->Shutdown();
            }
            return m_channel != nullptr;
        }

        ptrdiff_t SshTunnel::Read(void* buffer, size_t length)
        {
            return m_channel ? m_channel->Read(buffer, length) : -1;
        }

        bool SshTunnel::Write(const void* data, size_t length)
        {
            return m_channel && m_channel->Write(data, length);
        }

        void SshTunnel::SetReceiveTimeout(int timeoutMs)
        {
            if (m_channel)
            {
                m_channel->SetReceiveTimeout(timeoutMs);
            }
            m_socket.SetReceiveTimeout(timeoutMs);
        }

        bool SshTunnel::TimedOut() const
        {
            return m_channel ? m_channel->TimedOut() : m_socket.TimedOut();
        }

        void SshTunnel::Shutdown()
        {
            m_shutDown = true;
            if (m_channel)
            {
                m_channel->Shutdown();
            }
            m_socket.Shutdown(); // wakes a connect, handshake or login in progress on another thread
        }
    } // namespace Net
} // namespace Marconi
