// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Test support for the datagram channel: an in-memory datagram link that loses, duplicates, delays and reorders
// packets from a seed (so a failure replays), and a pair of channels wired through it on a clock the test advances

#pragma once

#include <MarconiCore/Wire/DatagramChannel.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace MarconiTest
{
    // xorshift64: the same numbers every run for a seed.
    class DatagramRandom
    {
    public:
        explicit DatagramRandom(uint64_t seed) : m_state(seed * 2654435761ull + 88172645463325252ull) {}
        uint64_t Next()
        {
            m_state ^= m_state << 13;
            m_state ^= m_state >> 7;
            m_state ^= m_state << 17;
            return m_state;
        }
        // 0 to 1
        double Unit() { return static_cast<double>(Next() >> 11) / 9007199254740992.0; }
        uint32_t Below(uint32_t limit) { return limit == 0 ? 0 : static_cast<uint32_t>(Next() % limit); }
        Marconi::Wire::Bytes Bytes(size_t count)
        {
            Marconi::Wire::Bytes bytes(count);
            for (uint8_t& byte : bytes)
            {
                byte = static_cast<uint8_t>(Next() >> 24);
            }
            return bytes;
        }

    private:
        uint64_t m_state;
    };

    // Direction 0 is client to server, 1 is server to client.
    class FakeLink
    {
    public:
        struct Params
        {
            double loss = 0;      // chance a datagram never arrives
            double duplicate = 0; // chance it arrives twice
            double reorder = 0;   // chance it is held back by up to reorderExtraUs
            int64_t delayUs = 0;
            int64_t jitterUs = 0;
            int64_t reorderExtraUs = 30000;
            bool blocked = false; // nothing gets through
        };

        explicit FakeLink(uint32_t seed) : m_random(seed) {}

        Params params;
        // Called with each datagram as sent; may change it; false drops it.
        std::function<bool(int direction, Marconi::Wire::Bytes& datagram)> filter;
        std::vector<Marconi::Wire::Bytes> sentByDirection[2]; // every datagram sent, before loss, if capture is on
        bool capture = false;
        size_t maxSize = 0;
        size_t sentCount[2] = {0, 0};

        void Send(int direction, const uint8_t* data, size_t size, int64_t now)
        {
            ++sentCount[direction];
            maxSize = std::max(maxSize, size);
            Marconi::Wire::Bytes datagram(data, data + size);
            if (capture)
            {
                sentByDirection[direction].push_back(datagram);
            }
            if (params.blocked || (filter && !filter(direction, datagram)) || m_random.Unit() < params.loss)
            {
                return;
            }
            int copies = m_random.Unit() < params.duplicate ? 2 : 1;
            for (int i = 0; i < copies; ++i)
            {
                int64_t delay = params.delayUs;
                if (params.jitterUs > 0)
                {
                    delay += static_cast<int64_t>(m_random.Unit() * params.jitterUs);
                }
                if (m_random.Unit() < params.reorder)
                {
                    delay += static_cast<int64_t>(m_random.Unit() * params.reorderExtraUs);
                }
                m_queue.push_back({now + delay, m_sequence++, direction, datagram});
            }
        }

        // Hands over everything due at `now`, earliest first.
        void Deliver(int64_t now, const std::function<void(int direction, const Marconi::Wire::Bytes&)>& deliver)
        {
            for (;;)
            {
                size_t best = m_queue.size();
                for (size_t i = 0; i < m_queue.size(); ++i)
                {
                    if (m_queue[i].due <= now && (best == m_queue.size() || Earlier(m_queue[i], m_queue[best])))
                    {
                        best = i;
                    }
                }
                if (best == m_queue.size())
                {
                    return;
                }
                Item item = std::move(m_queue[best]);
                m_queue.erase(m_queue.begin() + static_cast<ptrdiff_t>(best));
                deliver(item.direction, item.data);
            }
        }

        DatagramRandom& Random() { return m_random; }

    private:
        struct Item
        {
            int64_t due;
            uint64_t sequence;
            int direction;
            Marconi::Wire::Bytes data;
        };
        static bool Earlier(const Item& a, const Item& b)
        {
            return a.due != b.due ? a.due < b.due : a.sequence < b.sequence;
        }
        DatagramRandom m_random;
        std::vector<Item> m_queue;
        uint64_t m_sequence = 0;
    };

    // A client and a server channel joined by a FakeLink, and a control channel that never loses anything.
    struct DatagramPair
    {
        using Bytes = Marconi::Wire::Bytes;

        FakeLink link;
        int64_t now = 1000000;
        std::unique_ptr<Marconi::Wire::DatagramChannel> client;
        std::unique_ptr<Marconi::Wire::DatagramChannel> server;
        std::vector<Marconi::Wire::Delivery> clientGot;
        std::vector<Marconi::Wire::Delivery> serverGot;
        std::vector<Marconi::Wire::Gap> clientGaps;
        std::vector<Marconi::Wire::Gap> serverGaps;
        std::vector<std::pair<int64_t, Marconi::Wire::LinkState>> clientStates;
        std::vector<std::pair<int64_t, Marconi::Wire::LinkState>> serverStates;
        bool controlOpen = true; // false: control messages are dropped
        size_t controlSent = 0;

        static Bytes DefaultSecret()
        {
            Bytes secret(32);
            for (size_t i = 0; i < secret.size(); ++i)
            {
                secret[i] = static_cast<uint8_t>(i * 7 + 1);
            }
            return secret;
        }

        static Marconi::Wire::DatagramConfig DefaultConfig()
        {
            Marconi::Wire::DatagramConfig config;
            config.channels = {{1, Marconi::Wire::ChannelKind::Ordered},
                               {2, Marconi::Wire::ChannelKind::LatestWins},
                               {3, Marconi::Wire::ChannelKind::Reliable}};
            return config;
        }

        DatagramPair(const Marconi::Wire::DatagramConfig& config, uint32_t seed, Bytes serverSecret = DefaultSecret(),
                     Bytes clientSecret = DefaultSecret(), uint32_t sessionId = 0x1234abcd)
            : link(seed)
        {
            using namespace Marconi::Wire;
            client.reset(new DatagramChannel(
                DatagramChannel::Role::Client, sessionId, clientSecret, config,
                [this](const uint8_t* d, size_t n) { link.Send(0, d, n, now); },
                [this](const Bytes& m) { QueueControl(0, m); }));
            server.reset(new DatagramChannel(
                DatagramChannel::Role::Server, sessionId, serverSecret, config,
                [this](const uint8_t* d, size_t n) { link.Send(1, d, n, now); },
                [this](const Bytes& m) { QueueControl(1, m); }));
            DatagramChannel::Handler clientHandler;
            clientHandler.onMessage = [this](const Delivery& d) { clientGot.push_back(d); };
            clientHandler.onGap = [this](const Gap& g) { clientGaps.push_back(g); };
            clientHandler.onStateChanged = [this](LinkState, LinkState to) { clientStates.emplace_back(now, to); };
            client->SetHandler(clientHandler);
            DatagramChannel::Handler serverHandler;
            serverHandler.onMessage = [this](const Delivery& d) { serverGot.push_back(d); };
            serverHandler.onGap = [this](const Gap& g) { serverGaps.push_back(g); };
            serverHandler.onStateChanged = [this](LinkState, LinkState to) { serverStates.emplace_back(now, to); };
            server->SetHandler(serverHandler);
        }

        void StartBoth()
        {
            server->Start(now);
            client->Start(now);
        }

        void Step(int64_t us)
        {
            using namespace Marconi::Wire;
            now += us;
            link.Deliver(now,
                         [this](int direction, const Bytes& d)
                         {
                             DatagramChannel& to = direction == 0 ? *server : *client;
                             to.OnDatagram(d.data(), d.size(), now);
                         });
            while (!m_control.empty())
            {
                auto item = std::move(m_control.front());
                m_control.pop_front();
                Reader frame(item.second);
                uint64_t length = frame.Varint();
                Reader body(item.second.data() + (item.second.size() - frame.Remaining()),
                            static_cast<size_t>(length));
                Type type = ReadType(body);
                (item.first == 0 ? *server : *client).OnControlMessage(type, body, now);
            }
            client->Tick(now);
            server->Tick(now);
        }

        void Run(int64_t us, int64_t stepUs = 1000)
        {
            for (int64_t elapsed = 0; elapsed < us; elapsed += stepUs)
            {
                Step(stepUs);
            }
        }

        // Runs until `condition` or the time is up; returns whether the condition came true.
        bool RunUntil(const std::function<bool()>& condition, int64_t maxUs, int64_t stepUs = 1000)
        {
            for (int64_t elapsed = 0; elapsed < maxUs; elapsed += stepUs)
            {
                if (condition())
                {
                    return true;
                }
                Step(stepUs);
            }
            return condition();
        }

    private:
        void QueueControl(int fromDirection, const Bytes& message)
        {
            ++controlSent;
            if (controlOpen)
            {
                m_control.emplace_back(fromDirection == 0 ? 0 : 1, message);
            }
        }
        // (the side that receives it, the framed bytes): a client's message goes to the server (0)
        std::deque<std::pair<int, Bytes>> m_control;
    };
} // namespace MarconiTest
