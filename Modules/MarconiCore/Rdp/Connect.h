// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP milestones 1-2 end to end over a real connection: the X.224/TLS negotiation, the TLS upgrade, and (when the
// server asks for it) the CredSSP login. On success the connection is authenticated and ready for the MCS
// connection sequence (milestone 3). See Documentation/RDP.md.

#pragma once

#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rdp/CredSspExchange.h>
#include <MarconiCore/Rdp/Sequence.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Net
    {
        class Socket;
    }
    namespace Rdp
    {
        struct ConnectOutcome
        {
            bool reachedServer = false;   // the TCP connection was made and the server answered the X.224 request
            uint32_t selectedProtocol = 0; // the security the server chose (SecurityProtocol bits)
            bool encrypted = false;        // the TLS handshake completed
            std::string tlsName;           // e.g. "TLS 1.3"
            bool authenticated = false;    // CredSSP succeeded, or none was required
            bool usedNla = false;          // the server asked for Network Level Authentication
            uint32_t ntStatus = 0;         // the server's status on an NLA failure (e.g. 0xC000006D wrong password)
            bool reachedActive = false;    // the connection sequence completed: the server will now send output
            uint16_t ioChannelId = 0;      // the MCS channel the desktop rides on, once known
            uint32_t shareId = 0;          // the share id the server assigned
        };

        // Holds an authenticated RDP connection: the socket and the TLS stream stay open for the caller to continue
        // the connection sequence over. Destroying it closes them.
        class Connection
        {
        public:
            Connection();
            ~Connection();
            Connection(const Connection&) = delete;
            Connection& operator=(const Connection&) = delete;

            Net::TlsStream& Tls() { return *m_tls; }
            const ConnectOutcome& Outcome() const { return m_outcome; }

            // Runs the negotiation, TLS upgrade and (if asked) the CredSSP login. False with `error` on failure;
            // `Outcome()` says how far it got either way. `trustDecision` is asked when the server's certificate
            // isn't already trusted (RDP servers are usually self-signed).
            bool Open(const std::string& host, uint16_t port, const CredSspCredentials& credentials,
                      std::function<bool(const Net::TlsCertificate&)> trustDecision, int timeoutMs,
                      std::string& error, const std::atomic<bool>* cancel = nullptr);

            // Runs the MCS connection sequence and the capability exchange over the open, authenticated stream, up
            // to the point where the server starts sending output. `width`/`height` and `clientName` go in the
            // client data; `logonUser`/`logonPassword`/`logonDomain` go in the Client Info packet. False with
            // `error` on failure; Outcome().reachedActive says whether it finished.
            bool Activate(uint16_t width, uint16_t height, const std::string& clientName, const LogonInfo& logon,
                          std::string& error);

        private:
            bool SendFramed(const Bytes& mcsPdu, std::string& error);
            bool ReadFramed(Bytes& mcsPdu, std::string& error);
            bool JoinChannel(uint16_t channelId, std::string& error);

            uint16_t m_userChannelId = 0;
            uint16_t m_ioChannelId = 0;

            std::unique_ptr<Net::Socket> m_socket;
            std::unique_ptr<Net::TlsStream> m_tls;
            ConnectOutcome m_outcome;
        };
    } // namespace Rdp
} // namespace Marconi
