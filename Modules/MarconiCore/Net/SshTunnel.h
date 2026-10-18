// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A connection through SSH: log in to the other computer's SSH server and have it connect to the VNC server for us,
// so the whole session travels encrypted inside SSH

#pragma once

#include <MarconiCore/Net/Socket.h>

#include <Crypto/Ssh.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Net
    {
        // What the user is asked about when the SSH server's key isn't the one trusted before.
        struct SshHostKeyQuestion
        {
            std::string host;
            std::string algorithm;   // e.g. "ssh-ed25519"
            std::string fingerprint; // "SHA256:..."
            std::string previous;    // the fingerprint trusted before, when the key has changed; empty the first time
        };

        struct SshTunnelOptions
        {
            std::string host; // the SSH server
            uint16_t port = 22;
            std::string trustedFingerprint; // a host key accepted before; any other goes to trustDecision
            // Asked, on the calling thread, when the host key isn't the trusted one. Unset: only the trusted key
            // (or, with none trusted, nothing) is accepted.
            std::function<bool(const SshHostKeyQuestion&)> trustDecision;
            int timeoutMs = 10000;
        };

        // Connect, Login and Open run on one thread, in that order. Afterwards Read may run on one thread while
        // Write runs on another, as the RFB client does; Shutdown may be called from any thread.
        class SshTunnel
        {
        public:
            SshTunnel();
            ~SshTunnel();
            SshTunnel(const SshTunnel&) = delete;
            SshTunnel& operator=(const SshTunnel&) = delete;

            // Connects to the SSH server and checks its host key. False with `error` on failure; `refused` is also
            // set when the key was turned down, which is the user's decision rather than a fault.
            bool Connect(const SshTunnelOptions& options, std::string& error, bool& refused,
                         const std::atomic<bool>* cancel = nullptr);
            bool Login(const std::string& user, const std::string& password, std::string& error);
            // Has the SSH server connect to `host`:`port` (as seen from it) and carries that connection from now on.
            bool Open(const std::string& host, uint16_t port, std::string& error);

            const std::string& GetFingerprint() const { return m_fingerprint; }

            // As Socket::Receive and Socket::SendAll.
            ptrdiff_t Read(void* buffer, size_t length);
            bool Write(const void* data, size_t length);
            void SetReceiveTimeout(int timeoutMs);
            bool TimedOut() const;
            // Ends the connection: a blocked Read returns 0. Any thread.
            void Shutdown();

        private:
            Socket m_socket;
            Crypto::SshSession m_session;
            std::unique_ptr<Crypto::SshChannel> m_channel; // after the session: destroyed before it
            std::string m_fingerprint;
            std::atomic<bool> m_shutDown{false};
        };
    } // namespace Net
} // namespace Marconi
