// Copyright (c) 2026 Red Marmoset Studios Ltd.
// An RDP connection end to end: negotiation, TLS upgrade, the CredSSP login, the connection sequence, and the framed
// PDUs of the live session.

#include <MarconiCore/Rdp/Connect.h>

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Rdp/ClientData.h>
#include <MarconiCore/Rdp/License.h>
#include <MarconiCore/Rdp/Mcs.h>
#include <MarconiCore/Rdp/Negotiation.h>
#include <MarconiCore/Rdp/Pdu.h>
#include <MarconiCore/Rdp/Security.h>
#include <MarconiCore/Rdp/Sequence.h>
#include <MarconiCore/Text.h>

#include <Crypto/Crypto.h>
#include <Log/Log.h>

#include <algorithm>

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
            m_tls = createTlsStream ? createTlsStream(*m_socket, tlsOptions)
                                    : Net::CreateTlsStream(*m_socket, tlsOptions);
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

        bool Connection::ReadExact(uint8_t* buffer, size_t count)
        {
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
        }

        bool Connection::ReadFramed(Bytes& mcsPdu, std::string& error)
        {
            bool fastPath = false;
            if (!ReadPdu(mcsPdu, fastPath, error))
            {
                return false;
            }
            if (fastPath)
            {
                error = "the server sent a fast-path update before the connection was active";
                return false;
            }
            return true;
        }

        bool Connection::ReadPdu(Bytes& payload, bool& fastPath, std::string& error)
        {
            uint8_t first = 0;
            if (!ReadExact(&first, 1))
            {
                error = "the connection was closed";
                return false;
            }
            if (first == 0x03)
            {
                // A TPKT frame: version 3, reserved, then the big-endian total length.
                uint8_t header[4] = {first, 0, 0, 0};
                if (!ReadExact(header + 1, 3))
                {
                    error = "the connection dropped while reading a frame";
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
                if (!ReadExact(frame.data() + 4, total - 4))
                {
                    error = "the connection dropped while reading a frame";
                    return false;
                }
                if (!UnframeX224Data(frame, payload))
                {
                    error = "the server's RDP frame didn't parse";
                    return false;
                }
                fastPath = false;
                return true;
            }
            if ((first & 0x03) != 0)
            {
                error = "the server sent something that wasn't an RDP frame";
                return false;
            }
            // A fast-path output PDU (MS-RDPBCGR 2.2.9.1.2): the header byte, a 1- or 2-byte total length, then
            // (with TLS doing the encryption, no signature) the updates.
            uint8_t length1 = 0;
            if (!ReadExact(&length1, 1))
            {
                error = "the connection dropped while reading a frame";
                return false;
            }
            size_t total = length1;
            size_t headerBytes = 2;
            if (length1 & 0x80)
            {
                uint8_t length2 = 0;
                if (!ReadExact(&length2, 1))
                {
                    error = "the connection dropped while reading a frame";
                    return false;
                }
                total = (static_cast<size_t>(length1 & 0x7F) << 8) | length2;
                headerBytes = 3;
            }
            if (total < headerBytes || total > 1024 * 1024)
            {
                error = "the server sent an RDP frame with an implausible length";
                return false;
            }
            if ((first & 0xC0) != 0)
            {
                error = "the server sent a fast-path update with RDP encryption, which Marconi doesn't support";
                return false;
            }
            payload.resize(total - headerBytes);
            if (!payload.empty() && !ReadExact(payload.data(), payload.size()))
            {
                error = "the connection dropped while reading a frame";
                return false;
            }
            fastPath = true;
            return true;
        }

        bool Connection::SendPdu(const Bytes& pdu, std::string& error)
        {
            if (!m_tls)
            {
                error = "not connected";
                return false;
            }
            return SendFramed(BuildSendDataRequest(m_userChannelId, m_ioChannelId, pdu), error);
        }

        bool Connection::SendActivation(uint32_t shareId, uint16_t width, uint16_t height, std::string& error)
        {
            return SendPdu(BuildConfirmActive(shareId, m_userChannelId, width, height), error) &&
                   SendPdu(BuildSynchronize(shareId, m_userChannelId, m_ioChannelId), error) &&
                   SendPdu(BuildControlCooperate(shareId, m_userChannelId), error) &&
                   SendPdu(BuildControlRequest(shareId, m_userChannelId), error) &&
                   SendPdu(BuildFontList(shareId, m_userChannelId), error);
        }

        void Connection::Shutdown()
        {
            m_socket->Shutdown();
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

        bool Connection::HandleLicensing(const Bytes& pdu, uint8_t messageType, const std::string& user,
                                         const std::string& machine, std::string& error)
        {
            switch (messageType)
            {
                case kLicenseRequest:
                {
                    ServerLicenseRequest request;
                    if (!ParseServerLicenseRequest(pdu, request, error))
                    {
                        return false;
                    }
                    uint8_t clientRandom[32];
                    uint8_t premaster[48];
                    if (!Crypto::RandomBytes(clientRandom, sizeof(clientRandom)) ||
                        !Crypto::RandomBytes(premaster, sizeof(premaster)))
                    {
                        error = "couldn't make the random values the licensing exchange needs";
                        return false;
                    }
                    LOG_INFO("Rdp", "Server License Request received (%zu-bit key); answering with a New License "
                             "Request", request.modulus.size() * 8);
                    return SendPdu(BuildNewLicenseRequest(request, clientRandom, premaster, user, machine), error);
                }
                case kLicenseErrorAlert:
                {
                    uint32_t code = 0;
                    uint32_t transition = 0;
                    if (ParseLicenseErrorAlert(pdu, code, transition) && code == kStatusValidClient)
                    {
                        LOG_INFO("Rdp", "Licensing done: the server accepts this client");
                        return true;
                    }
                    error = LicenseErrorText(code);
                    return false;
                }
                case kPlatformChallenge:
                    error = "The server wants a Remote Desktop licence from a licensing server, which Marconi can't "
                            "provide yet.";
                    return false;
                case kNewLicense:
                case kUpgradeLicense:
                    LOG_INFO("Rdp", "Licensing done: the server issued a licence (not kept)");
                    return true;
                default:
                    LOG_INFO("Rdp", "Skipping a licensing message of type 0x%02x", messageType);
                    return true;
            }
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
                // Settings, not secrets: the start of it is what's needed to see why.
                Bytes start(response.begin(), response.begin() + std::min<size_t>(response.size(), 96));
                LOG_WARNING("Rdp", "Unreadable settings response (%zu bytes): %s", response.size(),
                            BytesToHex(start).c_str());
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

            // Wait for the server's Demand Active. Before it, the server runs the licensing exchange: its License
            // Request is answered with a New License Request, and its verdict read; anything else is skipped.
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
                    LOG_INFO("Rdp", "Skipping a %zu-byte MCS PDU (0x%02x) while waiting for Demand Active", pdu.size(),
                             pdu.empty() ? 0 : pdu[0]);
                    continue; // not a data channel message (e.g. a disconnect provider ultimatum would end here)
                }
                uint8_t licenseType = 0;
                if (IsLicensingPdu(payload, licenseType))
                {
                    if (!HandleLicensing(payload, licenseType, logon.user, clientName, error))
                    {
                        return false;
                    }
                    continue;
                }
                DemandActiveInfo demand;
                if (ParseDemandActiveInfo(payload, demand))
                {
                    shareId = demand.shareId;
                    m_outcome.shareId = shareId;
                    m_outcome.desktopWidth = demand.desktopWidth;
                    m_outcome.desktopHeight = demand.desktopHeight;
                    LOG_INFO("Rdp", "Server Demand Active received (share 0x%x, %u capability sets, desktop %ux%u at "
                             "%u bpp)", shareId, demand.capabilitySetCount, demand.desktopWidth, demand.desktopHeight,
                             demand.preferredBitsPerPixel);
                    break;
                }
                LOG_INFO("Rdp", "Skipping a %zu-byte PDU on channel %u (flags 0x%04x) while waiting for Demand Active",
                         payload.size(), channel, payload.size() >= 2 ? (payload[0] | (payload[1] << 8)) : 0);
            }
            if (shareId == 0)
            {
                error = "the server never sent its capabilities (Demand Active)";
                return false;
            }

            // Confirm Active with our capabilities (agreeing to the server's desktop size when it named one), then
            // the finalisation PDUs.
            if (m_outcome.desktopWidth != 0 && m_outcome.desktopHeight != 0)
            {
                width = m_outcome.desktopWidth;
                height = m_outcome.desktopHeight;
            }
            if (!SendActivation(shareId, width, height, error))
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
