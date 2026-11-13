// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The datagram channel: sealing and opening datagrams, fragmentation, the hello and keep-alive, statistics and the
// fall back to the control channel (see DatagramChannel.h)

#include <MarconiCore/Wire/DatagramChannel.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            constexpr uint64_t kMaxPacketNumber = 1ull << 48; // far below GCM's limits; the channel falls back before it
            constexpr Micros kBackoffResetAfterUs = 30000000;  // active this long: the next failure retries quickly again
        } // namespace

        DatagramChannel::DatagramChannel(Role role, uint32_t sessionId, const Bytes& secret,
                                         const DatagramConfig& config, SendDatagramFn sendDatagram,
                                         SendControlFn sendControl)
            : m_role(role), m_sessionId(sessionId), m_config(config), m_sendDatagram(std::move(sendDatagram)),
              m_sendControl(std::move(sendControl))
        {
            m_config.mtu = static_cast<uint16_t>(std::min<int>(std::max<int>(m_config.mtu, 100), 9000));
            m_backoff = m_config.retryInitialUs;
            if (secret.size() != kDatagramSecretBytes)
            {
                return;
            }
            DatagramKeys keys = DeriveDatagramKeys(secret, sessionId);
            const DirectionKeys& sending = role == Role::Client ? keys.clientToServer : keys.serverToClient;
            const DirectionKeys& receiving = role == Role::Client ? keys.serverToClient : keys.clientToServer;
            m_sendAead.reset(new DatagramAead(sending));
            m_receiveAead.reset(new DatagramAead(receiving));
            for (const ChannelSpec& spec : m_config.channels)
            {
                if (spec.kind != ChannelKind::Reliable)
                {
                    m_receivers[spec.id].reset(new ChannelReceiver(spec.id, spec.kind, m_config.receive));
                }
            }
            m_valid = m_sendAead->IsValid() && m_receiveAead->IsValid();
        }

        DatagramChannel::~DatagramChannel() = default;

        void DatagramChannel::SetHandler(Handler handler)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handler = std::move(handler);
        }

        const ChannelSpec* DatagramChannel::FindSpec(uint8_t channel) const
        {
            for (const ChannelSpec& spec : m_config.channels)
            {
                if (spec.id == channel)
                {
                    return &spec;
                }
            }
            return nullptr;
        }

        ChannelReceiver* DatagramChannel::FindReceiver(uint8_t channel)
        {
            auto it = m_receivers.find(channel);
            return it == m_receivers.end() ? nullptr : it->second.get();
        }

        size_t DatagramChannel::MaxUnfragmentedPayload() const
        {
            return m_config.mtu - kDatagramDataOverhead;
        }

        LinkState DatagramChannel::GetState() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_state;
        }

        DatagramStats DatagramChannel::GetStats() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            DatagramStats stats = m_stats;
            stats.state = m_state;
            stats.haveRtt = m_rtt.HasSample();
            stats.rttUs = m_rtt.Smoothed();
            stats.minRttUs = m_rtt.Minimum();
            stats.jitterUs = m_rtt.Variation();
            for (const auto& entry : m_receivers)
            {
                stats.messagesDelivered += entry.second->Delivered();
                stats.messagesDeadlineDropped += entry.second->DeadlineDropped();
                stats.messagesStale += entry.second->Stale();
                stats.gapsReported += entry.second->GapMessages();
            }
            return stats;
        }

        void DatagramChannel::Flush(std::unique_lock<std::mutex>& lock)
        {
            Outbox out = std::move(m_out);
            m_out = Outbox();
            Handler handler = m_handler;
            SendDatagramFn sendDatagram = m_sendDatagram;
            SendControlFn sendControl = m_sendControl;
            lock.unlock();
            if (sendDatagram)
            {
                for (const Bytes& datagram : out.datagrams)
                {
                    sendDatagram(datagram.data(), datagram.size());
                }
            }
            if (sendControl)
            {
                for (const Bytes& message : out.control)
                {
                    sendControl(message);
                }
            }
            if (handler.onStateChanged)
            {
                for (const auto& change : out.stateChanges)
                {
                    handler.onStateChanged(change.first, change.second);
                }
            }
            for (const ReceiveEvents::Event& event : out.events.items)
            {
                if (event.isGap)
                {
                    if (handler.onGap)
                    {
                        handler.onGap(event.gap);
                    }
                }
                else if (handler.onMessage)
                {
                    handler.onMessage(event.delivery);
                }
            }
        }

        void DatagramChannel::SetState(LinkState state, Micros now, bool notifyPeer)
        {
            if (state == m_state)
            {
                return;
            }
            LinkState previous = m_state;
            m_state = state;
            m_out.stateChanges.emplace_back(previous, state);
            if (state == LinkState::Active)
            {
                m_activeSince = now;
                m_lastReceive = now;
                m_lastReport = now;
                m_lastPing = now;
                m_retryAt = -1;
            }
            else if (state == LinkState::FellBack)
            {
                ++m_stats.fallbacks;
                m_retryAt = -1;
                if (m_role == Role::Client && m_config.autoRetry && !m_closed)
                {
                    m_retryAt = now + m_backoff;
                    m_backoff = std::min(m_config.retryMaxUs, m_backoff * 2);
                }
                if (notifyPeer && !m_closed)
                {
                    DatagramState notice;
                    notice.sessionId = m_sessionId;
                    notice.working = false;
                    m_out.control.push_back(notice.Encode());
                }
            }
        }

        void DatagramChannel::Start(Micros now)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_valid || m_closed)
            {
                return;
            }
            m_probeStart = now;
            m_lastHello = -1;
            m_lastSend = now;
            m_lastReceive = now;
            SetState(LinkState::Probing, now, false);
            if (m_role == Role::Client)
            {
                Writer hello;
                hello.U64(static_cast<uint64_t>(now));
                SendPacket(Kind::Hello, hello);
                m_lastHello = now;
            }
            Flush(lock);
        }

        void DatagramChannel::SendPacket(Kind kind, const Writer& body)
        {
            SendPacket(kind, body.Data().data(), body.Size());
        }

        void DatagramChannel::SendPacket(Kind kind, const uint8_t* body, size_t size)
        {
            if (!m_valid || m_sendPacketNumber >= kMaxPacketNumber)
            {
                return;
            }
            uint64_t packetNumber = m_sendPacketNumber++;
            Writer header;
            header.U8(1).U32(m_sessionId).U64(packetNumber);
            Bytes plain;
            plain.reserve(size + 1);
            plain.push_back(static_cast<uint8_t>(kind));
            plain.insert(plain.end(), body, body + size);
            Bytes packet = header.Take();
            packet.resize(kDatagramHeaderBytes + plain.size() + kDatagramTagBytes);
            if (!m_sendAead->Seal(packetNumber, packet.data(), kDatagramHeaderBytes, plain.data(), plain.size(),
                                  packet.data() + kDatagramHeaderBytes))
            {
                return;
            }
            ++m_stats.packetsSent;
            m_stats.bytesSent += packet.size();
            m_out.datagrams.push_back(std::move(packet));
        }

        DatagramChannel::ReceiveResult DatagramChannel::OnDatagram(const uint8_t* data, size_t size, Micros now)
        {
            ReceiveResult result;
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_valid || m_closed)
            {
                return result;
            }
            if (size < kDatagramHeaderBytes + kDatagramTagBytes + 1 || data[0] != 1)
            {
                ++m_stats.malformed;
                return result;
            }
            Reader header(data + 1, kDatagramHeaderBytes - 1);
            uint32_t sessionId = header.U32();
            uint64_t packetNumber = header.U64();
            if (sessionId != m_sessionId)
            {
                ++m_stats.wrongSession;
                return result;
            }
            if (!m_replay.WouldAccept(packetNumber))
            {
                ++m_stats.replaysRejected;
                return result;
            }
            Bytes plain(size - kDatagramHeaderBytes - kDatagramTagBytes);
            if (!m_receiveAead->Open(packetNumber, data, kDatagramHeaderBytes, data + kDatagramHeaderBytes,
                                     size - kDatagramHeaderBytes, plain.data()))
            {
                ++m_stats.authFailures;
                return result;
            }
            bool newest = !m_replay.HasReceived() || packetNumber > m_replay.Highest();
            m_replay.Mark(packetNumber);
            ++m_stats.packetsReceived;
            m_stats.bytesReceived += size;
            ++m_intervalReceived;
            m_lastReceive = now;
            result.accepted = true;
            result.newest = newest;
            try
            {
                Reader body(plain);
                Kind kind = static_cast<Kind>(body.U8());
                HandlePacket(kind, body, now);
            }
            catch (const WireError&)
            {
                ++m_stats.malformed;
            }
            Flush(lock);
            return result;
        }

        void DatagramChannel::HandlePacket(Kind kind, Reader& body, Micros now)
        {
            if (kind == Kind::Close)
            {
                m_closed = true;
                SetState(LinkState::FellBack, now, false);
                m_retryAt = -1;
                return;
            }
            if (kind < Kind::Hello || kind > Kind::Report)
            {
                throw WireError("unknown datagram kind");
            }
            // Anything that authenticated proves the path works both ways: the peer only sends once it has heard us.
            if (m_state != LinkState::Active)
            {
                SetState(LinkState::Active, now, false);
            }
            switch (kind)
            {
            case Kind::Hello:
            {
                Writer ack;
                ack.U64(body.U64());
                SendPacket(Kind::HelloAck, ack);
                m_lastSend = now;
                break;
            }
            case Kind::HelloAck:
            case Kind::Pong:
            {
                uint64_t token = body.U64();
                if (token <= static_cast<uint64_t>(now))
                {
                    m_rtt.Sample(now - static_cast<Micros>(token));
                }
                break;
            }
            case Kind::Ping:
            {
                Writer pong;
                pong.U64(body.U64());
                SendPacket(Kind::Pong, pong);
                m_lastSend = now;
                break;
            }
            case Kind::Data:
            {
                uint8_t channel = body.U8();
                uint32_t messageId = body.U32();
                uint16_t index = body.U16();
                uint16_t count = body.U16();
                ChannelReceiver* receiver = FindReceiver(channel);
                if (receiver == nullptr)
                {
                    ++m_stats.malformed; // a channel we have no use for
                    break;
                }
                size_t size = body.Remaining();
                Bytes fragment(size);
                body.Read(fragment.data(), size);
                if (!receiver->OnFragment(messageId, index, count, fragment.data(), size, now, m_out.events))
                {
                    ++m_stats.malformed;
                }
                break;
            }
            case Kind::Report:
            {
                uint32_t expected = body.U32();
                uint32_t received = body.U32();
                body.U32(); // the peer's gap and deadline-drop counts: informative, not used yet
                body.U32();
                if (expected > 0)
                {
                    uint32_t lost = expected > received ? expected - received : 0;
                    m_stats.peerLossPercent = 100.0 * lost / expected;
                    m_stats.havePeerReport = true;
                }
                break;
            }
            default:
                break;
            }
        }

        void DatagramChannel::SendReport(Micros now)
        {
            m_lastReport = now;
            if (!m_replay.HasReceived())
            {
                return;
            }
            uint64_t expected = m_replay.Highest() + 1 - std::min(m_intervalBase, m_replay.Highest() + 1);
            uint64_t received = m_intervalReceived;
            expected = std::max(expected, received);
            m_intervalBase = m_replay.Highest() + 1;
            m_intervalReceived = 0;
            double interval = expected > 0 ? 100.0 * static_cast<double>(expected - received) / expected : 0.0;
            m_stats.lossPercent = m_haveLossAverage ? 0.5 * m_stats.lossPercent + 0.5 * interval : interval;
            m_haveLossAverage = true;
            uint64_t gaps = 0;
            uint64_t drops = 0;
            for (const auto& entry : m_receivers)
            {
                gaps += entry.second->GapMessages();
                drops += entry.second->DeadlineDropped();
            }
            Writer report;
            report.U32(static_cast<uint32_t>(expected))
                .U32(static_cast<uint32_t>(received))
                .U32(static_cast<uint32_t>(gaps))
                .U32(static_cast<uint32_t>(drops));
            SendPacket(Kind::Report, report);
            m_lastSend = now;
        }

        void DatagramChannel::Tick(Micros now)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_valid)
            {
                return;
            }
            for (auto& entry : m_receivers)
            {
                entry.second->Tick(now, m_out.events);
            }
            switch (m_state)
            {
            case LinkState::Probing:
                if (m_role == Role::Client)
                {
                    if (now - m_probeStart >= m_config.probeTimeoutUs)
                    {
                        SetState(LinkState::FellBack, now, true);
                    }
                    else if (m_lastHello < 0 || now - m_lastHello >= m_config.helloIntervalUs)
                    {
                        Writer hello;
                        hello.U64(static_cast<uint64_t>(now));
                        SendPacket(Kind::Hello, hello);
                        m_lastHello = now;
                        m_lastSend = now;
                    }
                }
                else if (now - m_probeStart >= 2 * m_config.probeTimeoutUs)
                {
                    SetState(LinkState::FellBack, now, true);
                }
                break;
            case LinkState::Active:
                if (now - m_lastReceive >= m_config.deadAfterUs)
                {
                    SetState(LinkState::FellBack, now, true);
                    break;
                }
                if (now - m_activeSince >= kBackoffResetAfterUs)
                {
                    m_backoff = m_config.retryInitialUs;
                }
                if (now - m_lastReport >= m_config.reportIntervalUs)
                {
                    SendReport(now);
                }
                if (now - m_lastPing >= m_config.keepAliveUs)
                {
                    // Every interval, even when other packets went out: it keeps a NAT mapping open and measures
                    // the round trip.
                    Writer ping;
                    ping.U64(static_cast<uint64_t>(now));
                    SendPacket(Kind::Ping, ping);
                    m_lastPing = now;
                    m_lastSend = now;
                }
                break;
            case LinkState::FellBack:
                if (m_retryAt >= 0 && now >= m_retryAt && !m_closed)
                {
                    m_probeStart = now;
                    m_lastHello = -1;
                    m_retryAt = -1;
                    SetState(LinkState::Probing, now, false);
                    Writer hello;
                    hello.U64(static_cast<uint64_t>(now));
                    SendPacket(Kind::Hello, hello);
                    m_lastHello = now;
                    m_lastSend = now;
                }
                break;
            case LinkState::Idle:
                break;
            }
            Flush(lock);
        }

        void DatagramChannel::Close(Micros now)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_valid || m_closed)
            {
                return;
            }
            if (m_state == LinkState::Active)
            {
                SendPacket(Kind::Close, nullptr, 0);
            }
            m_closed = true;
            SetState(LinkState::FellBack, now, false);
            m_retryAt = -1;
            Flush(lock);
        }

        SendResult DatagramChannel::Send(uint8_t channel, const Bytes& payload, Micros now, uint32_t* messageIdOut)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const ChannelSpec* spec = FindSpec(channel);
            if (!m_valid || spec == nullptr)
            {
                return SendResult::Dropped;
            }
            if (payload.size() > m_config.receive.maxMessageBytes)
            {
                return SendResult::TooLarge;
            }
            uint32_t messageId = m_nextMessageId[channel]++;
            if (messageIdOut != nullptr)
            {
                *messageIdOut = messageId;
            }
            if (spec->kind == ChannelKind::Reliable || m_state != LinkState::Active)
            {
                if (!m_sendControl)
                {
                    return SendResult::Dropped;
                }
                DatagramData data;
                data.sessionId = m_sessionId;
                data.channel = channel;
                data.messageId = messageId;
                data.payload = payload;
                m_out.control.push_back(data.Encode());
                ++m_stats.messagesOverTcp;
                Flush(lock);
                return SendResult::Tcp;
            }
            size_t perFragment = MaxUnfragmentedPayload();
            size_t count = std::max<size_t>(1, (payload.size() + perFragment - 1) / perFragment);
            if (count > 0xFFFF)
            {
                return SendResult::TooLarge;
            }
            for (size_t i = 0; i < count; ++i)
            {
                size_t offset = i * perFragment;
                size_t length = std::min(perFragment, payload.size() - offset);
                Writer body;
                body.U8(channel).U32(messageId).U16(static_cast<uint16_t>(i)).U16(static_cast<uint16_t>(count));
                body.Raw(payload.data() + offset, length);
                SendPacket(Kind::Data, body);
            }
            m_lastSend = now;
            Flush(lock);
            return SendResult::Udp;
        }

        bool DatagramChannel::OnControlMessage(Type type, Reader& body, Micros now)
        {
            if (type != Type::DatagramState && type != Type::DatagramData)
            {
                return false;
            }
            std::unique_lock<std::mutex> lock(m_mutex);
            if (type == Type::DatagramState)
            {
                DatagramState state = DatagramState::Decode(body);
                if (state.sessionId == m_sessionId && !state.working && m_valid)
                {
                    SetState(LinkState::FellBack, now, false);
                }
                Flush(lock);
                return true;
            }
            DatagramData data = DatagramData::Decode(body);
            const ChannelSpec* spec = FindSpec(data.channel);
            if (data.sessionId != m_sessionId || spec == nullptr || !m_valid)
            {
                ++m_stats.malformed;
                return true;
            }
            ChannelReceiver* receiver = FindReceiver(data.channel);
            ReceiveEvents caughtUp; // what moving the datagram receiver past this message releases: after it
            if (receiver == nullptr || receiver->OnOutOfBand(data.messageId, now, caughtUp))
            {
                Delivery delivery;
                delivery.channel = data.channel;
                delivery.messageId = data.messageId;
                delivery.payload = std::move(data.payload);
                delivery.overTcp = true;
                // Gaps before this message come first, so the order is: what was lost, this message, what follows.
                ReceiveEvents ordered;
                std::vector<ReceiveEvents::Event> after;
                for (ReceiveEvents::Event& event : caughtUp.items)
                {
                    bool before = event.isGap && event.gap.firstMessageId < data.messageId;
                    (before ? ordered.items : after).push_back(std::move(event));
                }
                ordered.AddDelivery(std::move(delivery));
                for (ReceiveEvents::Event& event : after)
                {
                    ordered.items.push_back(std::move(event));
                }
                m_out.events.Append(std::move(ordered));
                ++m_stats.messagesOverTcp;
            }
            Flush(lock);
            return true;
        }
    } // namespace Wire
} // namespace Marconi
