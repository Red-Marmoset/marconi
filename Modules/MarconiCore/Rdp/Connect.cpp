// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP milestones 1-2 end to end: negotiation, TLS upgrade, and the CredSSP login over the TLS channel.

#include <MarconiCore/Rdp/Connect.h>

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Rdp/ClientData.h>
#include <MarconiCore/Rdp/Mcs.h>
#include <MarconiCore/Rdp/Negotiation.h>
#include <MarconiCore/Rdp/Pdu.h>
#include <MarconiCore/Rdp/Security.h>
#include <MarconiCore/Rdp/Sequence.h>

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

        void Connection::UseBlockingReads()
        {
            m_socket->SetReceiveTimeout(0);
        }

        void Connection::Shutdown()
        {
            m_socket->Shutdown();
        }

        bool Connection::SendOnIoChannel(const Bytes& sharePdu, std::string& error)
        {
            return SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId, sharePdu), error);
        }

        bool Connection::ReadOutput(Bytes& payload, bool& fastPath, std::string& error)
        {
            payload.clear();
            auto readExact = [&](uint8_t* buffer, size_t count) {
                size_t got = 0;
                while (got < count)
                {
                    ptrdiff_t n = m_tls->Read(buffer + got, count - got);
                    if (n <= 0)
                    {
                        return false;
                    }
                    got += static_cast<size_t>(n);
                }
                return true;
            };

            uint8_t first = 0;
            if (!readExact(&first, 1))
            {
                error = "the connection closed";
                return false;
            }

            if (first == 0x03) // a TPKT header: slow path
            {
                fastPath = false;
                uint8_t rest[3];
                if (!readExact(rest, 3))
                {
                    error = "the connection dropped mid-frame";
                    return false;
                }
                size_t total = (static_cast<size_t>(rest[1]) << 8) | rest[2];
                if (total < 7 || total > 16 * 1024 * 1024)
                {
                    error = "the server sent an RDP frame with an implausible length";
                    return false;
                }
                Bytes frame(total);
                frame[0] = first;
                frame[1] = rest[0];
                frame[2] = rest[1];
                frame[3] = rest[2];
                if (!readExact(frame.data() + 4, total - 4))
                {
                    error = "the connection dropped while reading a frame";
                    return false;
                }
                Bytes mcsPdu;
                if (!UnframeX224Data(frame, mcsPdu))
                {
                    error = "the server's RDP frame didn't parse";
                    return false;
                }
                uint16_t channel = 0;
                if (!StripSendData(mcsPdu, channel, payload))
                {
                    payload.clear(); // not a Send Data Indication (e.g. a disconnect ultimatum): caller loops on
                }
                return true;
            }

            // Otherwise it is a fast-path output header (MS-RDPBCGR 2.2.9.1.2): the first byte's low two bits are
            // the action (0 for fast path), then a 1- or 2-byte total length. TLS carries no FIPS or signature
            // fields, so the payload follows the length directly.
            fastPath = true;
            uint8_t len1 = 0;
            if (!readExact(&len1, 1))
            {
                error = "the connection dropped mid-frame";
                return false;
            }
            size_t total = 0;
            size_t headerLength = 2;
            if (len1 & 0x80)
            {
                uint8_t len2 = 0;
                if (!readExact(&len2, 1))
                {
                    error = "the connection dropped mid-frame";
                    return false;
                }
                total = (static_cast<size_t>(len1 & 0x7f) << 8) | len2;
                headerLength = 3;
            }
            else
            {
                total = len1;
            }
            if (total < headerLength || total > 16 * 1024 * 1024)
            {
                error = "the server sent a fast-path frame with an implausible length";
                return false;
            }
            size_t bodyLength = total - headerLength;
            payload.resize(bodyLength);
            if (bodyLength > 0 && !readExact(payload.data(), bodyLength))
            {
                error = "the connection dropped while reading a fast-path frame";
                return false;
            }
            return true;
        }

        bool Connection::SendFramed(const Bytes& mcsPdu, std::string& error)
        {
            Bytes frame = FrameX224Data(mcsPdu);
            if (!m_tls->Write(frame.data(), frame.size()))
            {
                error = "the connection dropped while sending";
                return false;
            }
            return true;
        }

        bool Connection::ReadFramed(Bytes& mcsPdu, std::string& error)
        {
            auto readExact = [&](uint8_t* buffer, size_t count) {
                size_t got = 0;
                while (got < count)
                {
                    ptrdiff_t n = m_tls->Read(buffer + got, count - got);
                    if (n <= 0)
                    {
                        return false;
                    }
                    got += static_cast<size_t>(n);
                }
                return true;
            };
            uint8_t header[4];
            if (!readExact(header, 4) || header[0] != 0x03)
            {
                error = "the server sent something that wasn't an RDP frame";
                return false;
            }
            size_t total = (static_cast<size_t>(header[2]) << 8) | header[3];
            if (total < 7 || total > 1024 * 1024)
            {
                error = "the server sent an RDP frame with an implausible length";
                return false;
            }
            Bytes frame(header, header + 4);
            frame.resize(total);
            if (!readExact(frame.data() + 4, total - 4))
            {
                error = "the connection dropped while reading a frame";
                return false;
            }
            if (!UnframeX224Data(frame, mcsPdu))
            {
                error = "the server's RDP frame didn't parse";
                return false;
            }
            return true;
        }

        bool Connection::JoinChannel(uint16_t channelId, std::string& error)
        {
            if (!SendFramed(BuildChannelJoinRequest(m_userChannelId, channelId), error))
            {
                return false;
            }
            Bytes confirm;
            uint16_t joined = 0;
            if (!ReadFramed(confirm, error) || !ParseChannelJoinConfirm(confirm, joined) || joined != channelId)
            {
                error = "the server didn't let us join a channel";
                return false;
            }
            return true;
        }

        bool Connection::Activate(uint16_t width, uint16_t height, const std::string& clientName,
                                  const LogonInfo& logon, std::string& error)
        {
            m_socket->SetReceiveTimeout(15000); // don't block for ever if the server stalls mid-sequence
            // Basic settings exchange: Connect-Initial with our client data, then the server's Connect-Response.
            ClientCoreSettings core;
            core.desktopWidth = width;
            core.desktopHeight = height;
            core.highColorDepth = 16;
            core.clientName = clientName;
            core.serverSelectedProtocol = m_outcome.selectedProtocol;
            Bytes clientData = BuildClientData(core, {});
            if (!SendFramed(BuildMcsConnectInitial(clientData), error))
            {
                return false;
            }
            Bytes response;
            if (!ReadFramed(response, error))
            {
                return false;
            }
            Bytes serverUserData;
            ServerData serverData;
            if (!ParseMcsConnectResponse(response, serverUserData) || !ParseServerData(serverUserData, serverData))
            {
                error = "the server's RDP settings response didn't parse";
                return false;
            }
            m_ioChannelId = serverData.ioChannelId != 0 ? serverData.ioChannelId : 1003;
            m_outcome.ioChannelId = m_ioChannelId;
            LOG_INFO("Rdp", "MCS settings exchanged (I/O channel %u, %zu virtual channels)", m_ioChannelId,
                     serverData.channelIds.size());

            // Channel setup: erect domain, attach user, join the user and I/O channels.
            if (!SendFramed(BuildErectDomainRequest(), error) || !SendFramed(BuildAttachUserRequest(), error))
            {
                return false;
            }
            Bytes attach;
            if (!ReadFramed(attach, error) || !ParseAttachUserConfirm(attach, m_userChannelId))
            {
                error = "the server didn't attach a user";
                return false;
            }
            if (!JoinChannel(m_userChannelId, error) || !JoinChannel(m_ioChannelId, error))
            {
                return false;
            }
            LOG_INFO("Rdp", "MCS channels joined (user %u, I/O %u)", m_userChannelId, m_ioChannelId);

            // Client Info (credentials), on the I/O channel.
            if (!SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId, BuildClientInfo(logon)), error))
            {
                return false;
            }
            LOG_INFO("Rdp", "Client Info sent; waiting for the server's capabilities");

            // Wait for the server's Demand Active (it may send a licensing PDU or error info first; skip those).
            uint32_t shareId = 0;
            for (int guard = 0; guard < 20; ++guard)
            {
                Bytes pdu;
                if (!ReadFramed(pdu, error))
                {
                    return false;
                }
                uint16_t channel = 0;
                Bytes payload;
                if (!StripSendData(pdu, channel, payload))
                {
                    continue; // not a data channel message (e.g. a disconnect provider ultimatum would end here)
                }
                uint16_t count = 0;
                if (ParseDemandActive(payload, shareId, count))
                {
                    m_outcome.shareId = shareId;
                    LOG_INFO("Rdp", "Server Demand Active received (share 0x%x, %u capability sets)", shareId, count);
                    break;
                }
            }
            if (shareId == 0)
            {
                error = "the server never sent its capabilities (Demand Active)";
                return false;
            }

            // Confirm Active with our capabilities, then the finalisation PDUs.
            if (!SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId,
                                                 BuildConfirmActive(shareId, m_userChannelId, width, height)), error) ||
                !SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId,
                                                 BuildSynchronize(shareId, m_userChannelId, m_ioChannelId)), error) ||
                !SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId,
                                                 BuildControlCooperate(shareId, m_userChannelId)), error) ||
                !SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId,
                                                 BuildControlRequest(shareId, m_userChannelId)), error) ||
                !SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId,
                                                 BuildFontList(shareId, m_userChannelId)), error))
            {
                return false;
            }

            // The sequence is done when the server sends the Font Map.
            for (int guard = 0; guard < 20; ++guard)
            {
                Bytes pdu;
                if (!ReadFramed(pdu, error))
                {
                    return false;
                }
                uint16_t channel = 0;
                Bytes payload;
                if (!StripSendData(pdu, channel, payload))
                {
                    continue;
                }
                ShareDataType type2;
                Bytes dataBody;
                if (ReadShareData(payload, type2, dataBody) && type2 == ShareDataType::FontMap)
                {
                    m_outcome.reachedActive = true;
                    LOG_INFO("Rdp", "RDP connection is active (share 0x%x, I/O channel %u)", shareId, m_ioChannelId);
                    return true;
                }
            }
            error = "the server didn't finish the connection sequence (no Font Map)";
            return false;
        }
    } // namespace Rdp
} // namespace Marconi
