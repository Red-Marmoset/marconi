// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The datagram channel over UDP sockets (see DatagramSession.h)

#include <MarconiCore/Wire/DatagramSession.h>

#include <chrono>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            int64_t SteadyMicros()
            {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            }
        } // namespace

        Micros DatagramSession::Now() const
        {
            return SteadyMicros() - m_epoch;
        }

        std::unique_ptr<DatagramSession> DatagramSession::CreateServer(const DatagramConfig& config,
                                                                       const std::string& bindIpv4, uint16_t firstPort,
                                                                       uint16_t lastPort,
                                                                       DatagramChannel::SendControlFn sendControl,
                                                                       std::string& error)
        {
            std::unique_ptr<DatagramSession> session(new DatagramSession());
            session->m_isServer = true;
            session->m_epoch = SteadyMicros();
            bool bound = false;
            if (firstPort == 0 && lastPort == 0)
            {
                bound = session->m_worker.Bind(bindIpv4, 0, error);
            }
            else
            {
                for (uint32_t port = firstPort; port <= lastPort && !bound; ++port)
                {
                    bound = session->m_worker.Bind(bindIpv4, static_cast<uint16_t>(port), error);
                }
            }
            if (!bound)
            {
                return nullptr;
            }
            if (!DatagramOffer::Create(session->m_worker.GetLocalPort(), config.mtu, session->m_offer))
            {
                error = "The system's random number source failed";
                return nullptr;
            }
            Net::UdpWorker* worker = &session->m_worker;
            session->m_channel.reset(new DatagramChannel(
                DatagramChannel::Role::Server, session->m_offer.sessionId, session->m_offer.secret, config,
                [worker](const uint8_t* data, size_t size) { worker->SendToPeer(data, size); },
                std::move(sendControl)));
            if (!session->m_channel->IsValid())
            {
                error = "Could not set up the datagram encryption";
                return nullptr;
            }
            return session;
        }

        std::unique_ptr<DatagramSession> DatagramSession::CreateClient(const DatagramOffer& offer,
                                                                       const std::string& serverHost,
                                                                       const DatagramConfig& config,
                                                                       DatagramChannel::SendControlFn sendControl,
                                                                       std::string& error)
        {
            if (offer.version != kDatagramVersion || offer.port == 0)
            {
                error = "The server's datagram offer is not one this version understands";
                return nullptr;
            }
            std::unique_ptr<DatagramSession> session(new DatagramSession());
            session->m_isServer = false;
            session->m_offer = offer;
            session->m_epoch = SteadyMicros();
            std::string ip;
            if (!Net::ResolveIpv4(serverHost, ip, error) || !session->m_worker.Bind("0.0.0.0", 0, error))
            {
                return nullptr;
            }
            session->m_worker.SetPeer(ip, offer.port);
            DatagramConfig used = config;
            used.mtu = offer.mtu;
            Net::UdpWorker* worker = &session->m_worker;
            session->m_channel.reset(new DatagramChannel(
                DatagramChannel::Role::Client, offer.sessionId, offer.secret, used,
                [worker](const uint8_t* data, size_t size) { worker->SendToPeer(data, size); },
                std::move(sendControl)));
            if (!session->m_channel->IsValid())
            {
                error = "Could not set up the datagram encryption";
                return nullptr;
            }
            return session;
        }

        DatagramSession::~DatagramSession()
        {
            Stop();
        }

        void DatagramSession::Start()
        {
            if (m_started)
            {
                return;
            }
            m_started = true;
            m_channel->Start(Now());
            m_worker.Start(
                [this](const uint8_t* data, size_t size, const std::string& ip, uint16_t port)
                { OnReceive(data, size, ip, port); },
                [this]() { m_channel->Tick(Now()); });
        }

        void DatagramSession::Stop()
        {
            m_worker.Stop();
            m_started = false;
        }

        void DatagramSession::OnReceive(const uint8_t* data, size_t size, const std::string& fromIp, uint16_t fromPort)
        {
            DatagramChannel::ReceiveResult result = m_channel->OnDatagram(data, size, Now());
            if (m_isServer && result.accepted && result.newest)
            {
                m_worker.SetPeer(fromIp, fromPort);
            }
        }

        bool DatagramSession::OnControlMessage(Type type, Reader& body)
        {
            return m_channel->OnControlMessage(type, body, Now());
        }
    } // namespace Wire
} // namespace Marconi
