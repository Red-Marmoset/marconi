// Copyright (c) 2026 Red Marmoset Studios Ltd.
// One end of the datagram channel (SERVER_PLAN.md, Phase 5): encrypted, authenticated, fragmented datagrams with
// per-channel delivery rules, a hello that opens the path through a NAT, keep-alive, loss and round-trip
// statistics, and a fall back to the control channel when UDP doesn't get through.
//
// This class owns no socket and no thread. The caller gives it a function that sends a datagram and (optionally)
// one that sends a framed control message, and calls OnDatagram for what arrives, OnControlMessage for the control
// messages in 90 to 99, and Tick regularly (every 10 to 50 ms) with the current time in microseconds. That keeps it
// deterministic under test. DatagramSession (DatagramSession.h) wires it to UDP sockets and a worker thread.
//
// Datagram layout (the first 13 bytes are in the clear, and are the authenticated "associated data" of the AEAD):
//   u8 format (1) | u32 session id | u64 packet number | AES-256-GCM(ciphertext of the body) | 16-byte tag
// The body is: u8 kind, then by kind: Hello/HelloAck/Ping/Pong a u64 token; Data u8 channel, u32 message id,
// u16 fragment index, u16 fragment count, the fragment; Report u32 expected, u32 received, u32 gap messages,
// u32 deadline drops; Close nothing. Numbers are little-endian.
//
// Threading: every method is safe to call from any thread. Handler callbacks run on the thread that called the
// method that produced them, after the channel's lock is released, so a handler may call Send.

#pragma once

#include <MarconiCore/Wire/DatagramCrypto.h>
#include <MarconiCore/Wire/DatagramMessages.h>
#include <MarconiCore/Wire/DatagramReceiver.h>
#include <MarconiCore/Wire/DatagramStats.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace Marconi
{
    namespace Wire
    {
        // The bytes a Data datagram spends on header, channel, ids and tag; a message of up to
        // mtu - kDatagramDataOverhead bytes fits in one datagram.
        constexpr size_t kDatagramHeaderBytes = 13;
        constexpr size_t kDatagramDataOverhead = kDatagramHeaderBytes + 10 + kDatagramTagBytes;

        struct DatagramConfig
        {
            uint16_t mtu = 1200; // largest datagram (UDP payload) sent; clamped to 100..9000
            std::vector<ChannelSpec> channels;
            ReceiverLimits receive;
            Micros helloIntervalUs = 250000;  // client: how often Hello is repeated while probing
            Micros probeTimeoutUs = 3000000;  // client: give up on UDP after this long (the server waits twice as long)
            Micros keepAliveUs = 1000000;     // a Ping goes out this often while active (NAT keep-alive and RTT)
            Micros deadAfterUs = 6000000;     // nothing authentic heard for this long: UDP is gone
            Micros reportIntervalUs = 1000000; // how often the loss Report goes out
            Micros retryInitialUs = 5000000;  // client: first wait before trying UDP again after falling back
            Micros retryMaxUs = 120000000;    // ... doubling up to this
            bool autoRetry = true;
        };

        enum class SendResult : uint8_t
        {
            Udp,      // sent as datagrams
            Tcp,      // sent on the control channel (UDP not active, or a reliable channel)
            Dropped,  // not sent: unknown channel, or UDP not active and no control channel to use
            TooLarge  // over the message size limit
        };

        class DatagramChannel
        {
        public:
            enum class Role : uint8_t
            {
                Client,
                Server
            };
            using SendDatagramFn = std::function<void(const uint8_t* data, size_t size)>;
            using SendControlFn = std::function<void(const Bytes& framedMessage)>;

            struct Handler
            {
                std::function<void(const Delivery&)> onMessage;
                std::function<void(const Gap&)> onGap;
                std::function<void(LinkState from, LinkState to)> onStateChanged;
            };

            struct ReceiveResult
            {
                bool accepted = false; // authenticated and new
                bool newest = false;   // ... and its packet number is the highest so far: its source address is current
            };

            DatagramChannel(Role role, uint32_t sessionId, const Bytes& secret, const DatagramConfig& config,
                            SendDatagramFn sendDatagram, SendControlFn sendControl);
            ~DatagramChannel();

            bool IsValid() const { return m_valid; }
            void SetHandler(Handler handler);

            // Client: starts sending Hello. Server: starts waiting for it. Call after the offer was accepted.
            void Start(Micros now);
            // One datagram from the network. Never answers anything that doesn't authenticate.
            ReceiveResult OnDatagram(const uint8_t* data, size_t size, Micros now);
            // DatagramState and DatagramData (the body after the type byte). Other types are ignored (false).
            bool OnControlMessage(Type type, Reader& body, Micros now);
            void Tick(Micros now);
            // Tells the peer and stops; the state becomes FellBack and is not retried.
            void Close(Micros now);

            // Sends one message on a channel: as datagrams while UDP is active, on the control channel otherwise.
            SendResult Send(uint8_t channel, const Bytes& payload, Micros now, uint32_t* messageIdOut = nullptr);
            // The largest message that fits in one datagram, so a sender can keep small messages unfragmented.
            size_t MaxUnfragmentedPayload() const;

            LinkState GetState() const;
            DatagramStats GetStats() const;
            uint32_t GetSessionId() const { return m_sessionId; }

        private:
            struct Outbox
            {
                std::vector<Bytes> datagrams;
                std::vector<Bytes> control;
                ReceiveEvents events;
                std::vector<std::pair<LinkState, LinkState>> stateChanges;
            };
            enum class Kind : uint8_t
            {
                Hello = 1,
                HelloAck = 2,
                Ping = 3,
                Pong = 4,
                Data = 5,
                Report = 6,
                Close = 7
            };

            void SendPacket(Kind kind, const Writer& body);
            void SendPacket(Kind kind, const uint8_t* body, size_t size);
            void SetState(LinkState state, Micros now, bool notifyPeer);
            void HandlePacket(Kind kind, Reader& body, Micros now);
            void SendReport(Micros now);
            void Flush(std::unique_lock<std::mutex>& lock);
            ChannelReceiver* FindReceiver(uint8_t channel);
            const ChannelSpec* FindSpec(uint8_t channel) const;

            Role m_role;
            uint32_t m_sessionId;
            DatagramConfig m_config;
            SendDatagramFn m_sendDatagram;
            SendControlFn m_sendControl;
            Handler m_handler;
            bool m_valid = false;

            mutable std::mutex m_mutex;
            std::unique_ptr<DatagramAead> m_sendAead;
            std::unique_ptr<DatagramAead> m_receiveAead;
            uint64_t m_sendPacketNumber = 0;
            ReplayWindow m_replay;
            std::map<uint8_t, std::unique_ptr<ChannelReceiver>> m_receivers;
            std::map<uint8_t, uint32_t> m_nextMessageId;
            Outbox m_out;

            LinkState m_state = LinkState::Idle;
            bool m_closed = false;
            Micros m_probeStart = 0;
            Micros m_lastHello = -1;
            Micros m_lastSend = 0;
            Micros m_lastPing = 0;
            Micros m_lastReceive = 0;
            Micros m_lastReport = 0;
            Micros m_activeSince = 0;
            Micros m_retryAt = -1;
            Micros m_backoff = 0;

            // Loss of what we receive, measured per report interval from packet numbers.
            uint64_t m_intervalReceived = 0;
            uint64_t m_intervalBase = 0;
            bool m_haveLossAverage = false;

            RttEstimator m_rtt;
            DatagramStats m_stats;
        };
    } // namespace Wire
} // namespace Marconi
