// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The datagram channel on real UDP sockets: a DatagramChannel, a UdpWorker thread feeding it, and the setup from the
// control channel's DatagramOffer. The server creates one per viewer, the client one per offer it accepts.
//
// Setup, in the words of the protocol (the control channel is the TLS connection, so the secret never travels in
// the clear):
//   server: CreateServer(...) binds a UDP port and makes the offer; send GetOffer().Encode() to the client.
//   client: reads the DatagramOffer, CreateClient(offer, serverHost, ...), sends DatagramAccept, calls Start().
//   server: on the client's DatagramAccept calls Start(); the client's Hello arrives, the path is open.
// Control messages 92 and 93 that arrive afterwards go to OnControlMessage.
//
// The server answers whatever address an authenticated datagram last came from, so a client behind a NAT that
// changes its mapping keeps working without any new setup.

#pragma once

#include <MarconiCore/Net/UdpWorker.h>
#include <MarconiCore/Wire/DatagramChannel.h>

#include <memory>
#include <string>

namespace Marconi
{
    namespace Wire
    {
        class DatagramSession
        {
        public:
            // Binds a UDP port in [firstPort, lastPort] (0 and 0: any free port) on `bindIpv4` and makes the offer.
            static std::unique_ptr<DatagramSession> CreateServer(const DatagramConfig& config, const std::string& bindIpv4,
                                                                 uint16_t firstPort, uint16_t lastPort,
                                                                 DatagramChannel::SendControlFn sendControl,
                                                                 std::string& error);
            // `serverHost` is the address the control connection went to. Takes the mtu from the offer.
            static std::unique_ptr<DatagramSession> CreateClient(const DatagramOffer& offer,
                                                                 const std::string& serverHost,
                                                                 const DatagramConfig& config,
                                                                 DatagramChannel::SendControlFn sendControl,
                                                                 std::string& error);
            ~DatagramSession();

            const DatagramOffer& GetOffer() const { return m_offer; }
            DatagramChannel& Channel() { return *m_channel; }

            // Starts the receive thread and the channel (Hello from the client, waiting for it on the server).
            void Start();
            // Stops the thread, closes the socket. The destructor does this too.
            void Stop();
            // For control messages 92 and 93; false for any other type.
            bool OnControlMessage(Type type, Reader& body);

            // Microseconds on the clock the channel is driven with.
            Micros Now() const;

        private:
            DatagramSession() = default;
            void OnReceive(const uint8_t* data, size_t size, const std::string& fromIp, uint16_t fromPort);

            bool m_isServer = false;
            DatagramOffer m_offer;
            Net::UdpWorker m_worker;
            std::unique_ptr<DatagramChannel> m_channel;
            int64_t m_epoch = 0;
            bool m_started = false;
        };
    } // namespace Wire
} // namespace Marconi
