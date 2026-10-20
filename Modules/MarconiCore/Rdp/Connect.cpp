// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP milestones 1-2 end to end: negotiation, TLS upgrade, and the CredSSP login over the TLS channel.

#include <MarconiCore/Rdp/Connect.h>

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Rdp/Negotiation.h>
#include <MarconiCore/Rdp/Security.h>

#include <Log/Log.h>

namespace Marconi
{
    namespace Rdp
    {
        Connection::Connection() : m_socket(std::make_unique<Net::Socket>()) {}
        Connection::~Connection() = default;

        bool Connection::Open(const std::string& host, uint16_t port, const CredSspCredentials& credentials,
                              std::function<bool(const Net::TlsCertificate&)> trustDecision, int timeoutMs,
                              std::string& error, const std::atomic<bool>* cancel)
        {
            m_outcome = ConnectOutcome();
            if (!m_socket->Connect(host, port, timeoutMs, error, cancel))
            {
                error = "Couldn't reach the Remote Desktop server on " + host + ": " + error;
                return false;
            }
            m_socket->SetReceiveTimeout(timeoutMs);

            // Milestone 1: the X.224 security negotiation, over the raw socket.
            SecurityLink socketLink;
            socketLink.receive = [this](void* buffer, size_t length) { return m_socket->Receive(buffer, length); };
            socketLink.sendAll = [this](const void* data, size_t length) { return m_socket->SendAll(data, length); };
            NegotiationResponse negotiation;
            if (!NegotiateSecurity(socketLink, kTls | kCredSsp, "", negotiation, error))
            {
                return false;
            }
            m_outcome.reachedServer = true;
            if (negotiation.failed)
            {
                error = NegotiationFailureText(negotiation.failureCode);
                return false;
            }
            m_outcome.selectedProtocol = negotiation.hasNegotiation ? negotiation.selectedProtocol : kStandardRdp;
            if ((m_outcome.selectedProtocol & (kTls | kCredSsp)) == 0)
            {
                error = "The server wants the old RDP security layer, which Marconi doesn't support; it needs TLS.";
                return false;
            }

            // Milestones 1-2: TLS, then CredSSP over it.
            Net::TlsOptions tlsOptions;
            tlsOptions.hostName = host;
            tlsOptions.trustDecision = std::move(trustDecision);
            m_tls = Net::CreateTlsStream(*m_socket, tlsOptions);
            if (!m_tls)
            {
                error = "This build of Marconi can't make encrypted connections.";
                return false;
            }
            m_socket->SetReceiveTimeout(0); // the TLS and login steps can take as long as the server needs
            if (!m_tls->Handshake(error))
            {
                error = "The Remote Desktop server's TLS handshake failed: " + error;
                return false;
            }
            m_outcome.encrypted = true;
            m_outcome.tlsName = m_tls->GetProtocolName();

            if (m_outcome.selectedProtocol & kCredSsp)
            {
                m_outcome.usedNla = true;
                SecurityLink tlsLink;
                tlsLink.receive = [this](void* buffer, size_t length) { return m_tls->Read(buffer, length); };
                tlsLink.sendAll = [this](const void* data, size_t length) { return m_tls->Write(data, length); };
                if (!RunCredSsp(tlsLink, m_tls->GetPeerPublicKey(), credentials, m_outcome.ntStatus, error))
                {
                    return false;
                }
                m_outcome.authenticated = true;
            }
            else
            {
                // TLS without NLA: the credentials go later, in the RDP Client Info PDU (milestone 3).
                m_outcome.authenticated = true;
            }
            LOG_INFO("Rdp", "Connected to %s: %s%s", host.c_str(), m_outcome.tlsName.c_str(),
                     m_outcome.usedNla ? ", logged in (NLA)" : ", no NLA");
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
