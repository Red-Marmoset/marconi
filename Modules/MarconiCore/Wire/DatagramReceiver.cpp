// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Fragment reassembly and the latest-wins and ordered delivery rules (see DatagramReceiver.h)

#include <MarconiCore/Wire/DatagramReceiver.h>

#include <algorithm>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            // A message id this far ahead of the one expected is noise or an attack, not loss.
            constexpr int64_t kMaxAhead = 100000;
        } // namespace

        ChannelReceiver::ChannelReceiver(uint8_t channel, ChannelKind kind, const ReceiverLimits& limits)
            : m_channel(channel), m_kind(kind), m_limits(limits)
        {
        }

        int64_t ChannelReceiver::Unwrap(uint32_t messageId) const
        {
            // Ids are 32 bits on the wire and count up forever on the 64-bit side: pick the 64-bit value closest to
            // the one expected.
            int32_t difference = static_cast<int32_t>(messageId - static_cast<uint32_t>(m_next));
            return static_cast<int64_t>(m_next) + difference;
        }

        bool ChannelReceiver::Add(Partial& partial, uint16_t index, uint16_t count, const uint8_t* data, size_t size,
                                  Micros now)
        {
            if (partial.count == 0)
            {
                partial.count = count;
                partial.pieces.resize(count);
                partial.present.assign(count, 0);
                partial.firstSeen = now;
            }
            if (partial.count != count || index >= count)
            {
                return false;
            }
            if (partial.present[index] != 0)
            {
                return true; // a duplicate fragment
            }
            if (partial.bytes + size > m_limits.maxMessageBytes)
            {
                return false;
            }
            partial.pieces[index].assign(data, data + size);
            partial.present[index] = 1;
            partial.bytes += size;
            ++partial.have;
            return true;
        }

        Bytes ChannelReceiver::Join(const Partial& partial)
        {
            Bytes joined;
            joined.reserve(partial.bytes);
            for (const Bytes& piece : partial.pieces)
            {
                joined.insert(joined.end(), piece.begin(), piece.end());
            }
            return joined;
        }

        void ChannelReceiver::Deliver(uint64_t id, Partial& partial, ReceiveEvents& out)
        {
            Delivery delivery;
            delivery.channel = m_channel;
            delivery.messageId = static_cast<uint32_t>(id);
            delivery.payload = Join(partial);
            out.AddDelivery(std::move(delivery));
            ++m_delivered;
        }

        bool ChannelReceiver::OnFragment(uint32_t messageId, uint16_t index, uint16_t count, const uint8_t* data,
                                         size_t size, Micros now, ReceiveEvents& out)
        {
            if (count == 0 || index >= count)
            {
                return false;
            }
            int64_t id = Unwrap(messageId);
            if (id < static_cast<int64_t>(m_next))
            {
                ++m_stale; // a fragment of a message already delivered, dropped or superseded
                return true;
            }
            if (id - static_cast<int64_t>(m_next) > kMaxAhead)
            {
                return false;
            }
            uint64_t key = static_cast<uint64_t>(id);
            Partial& partial = m_partials[key];
            if (!Add(partial, index, count, data, size, now))
            {
                m_partials.erase(key);
                return false;
            }
            if (m_kind == ChannelKind::LatestWins)
            {
                if (partial.have == partial.count)
                {
                    Deliver(key, partial, out);
                    // Everything older, complete or not, is now superseded.
                    for (auto it = m_partials.begin(); it != m_partials.end() && it->first <= key;)
                    {
                        if (it->first != key)
                        {
                            ++m_stale;
                        }
                        it = m_partials.erase(it);
                    }
                    m_next = key + 1;
                }
                else if (m_partials.size() > m_limits.maxPendingMessages)
                {
                    m_partials.erase(m_partials.begin());
                    ++m_deadlineDropped;
                }
                return true;
            }
            AdvanceOrdered(now, out);
            return true;
        }

        void ChannelReceiver::AdvanceOrdered(Micros now, ReceiveEvents& out)
        {
            for (;;)
            {
                auto head = m_partials.find(m_next);
                if (head != m_partials.end() && head->second.have == head->second.count)
                {
                    Deliver(m_next, head->second, out);
                    m_partials.erase(head);
                    ++m_next;
                    m_waitingSince = -1;
                    continue;
                }
                auto newer = m_partials.upper_bound(m_next);
                bool newerExists = newer != m_partials.end();
                bool headExpired = head != m_partials.end() && now - head->second.firstSeen >= m_limits.deadlineUs;
                bool newerExpired = false;
                if (newerExists)
                {
                    if (m_waitingSince < 0)
                    {
                        m_waitingSince = now;
                    }
                    newerExpired = now - m_waitingSince >= m_limits.deadlineUs;
                }
                bool forced = m_partials.size() > m_limits.maxPendingMessages;
                if (!(headExpired || newerExpired || forced))
                {
                    return;
                }
                uint64_t first = m_next;
                uint64_t lost = 0;
                if (head != m_partials.end())
                {
                    m_partials.erase(head);
                    ++m_deadlineDropped;
                    lost = 1;
                    m_next = first + 1;
                }
                auto present = m_partials.lower_bound(m_next);
                if (present != m_partials.end())
                {
                    lost += present->first - m_next;
                    m_next = present->first;
                }
                else if (lost == 0)
                {
                    return;
                }
                Gap gap;
                gap.channel = m_channel;
                gap.firstMessageId = static_cast<uint32_t>(first);
                gap.count = static_cast<uint32_t>(std::min<uint64_t>(lost, 0xFFFFFFFFu));
                out.AddGap(gap);
                m_gapMessages += lost;
                m_waitingSince = -1;
            }
        }

        void ChannelReceiver::Tick(Micros now, ReceiveEvents& out)
        {
            if (m_kind == ChannelKind::LatestWins)
            {
                for (auto it = m_partials.begin(); it != m_partials.end();)
                {
                    if (now - it->second.firstSeen >= m_limits.deadlineUs)
                    {
                        it = m_partials.erase(it);
                        ++m_deadlineDropped;
                    }
                    else
                    {
                        ++it;
                    }
                }
                return;
            }
            AdvanceOrdered(now, out);
        }

        bool ChannelReceiver::OnOutOfBand(uint32_t messageId, Micros now, ReceiveEvents& out)
        {
            int64_t id = Unwrap(messageId);
            if (id < static_cast<int64_t>(m_next) || id - static_cast<int64_t>(m_next) > kMaxAhead)
            {
                ++m_stale;
                return false;
            }
            uint64_t key = static_cast<uint64_t>(id);
            if (m_kind == ChannelKind::Ordered && key > m_next)
            {
                Gap gap;
                gap.channel = m_channel;
                gap.firstMessageId = static_cast<uint32_t>(m_next);
                gap.count = static_cast<uint32_t>(key - m_next);
                out.AddGap(gap);
                m_gapMessages += gap.count;
            }
            for (auto it = m_partials.begin(); it != m_partials.end() && it->first <= key;)
            {
                it = m_partials.erase(it);
            }
            m_next = key + 1;
            m_waitingSince = -1;
            if (m_kind == ChannelKind::Ordered)
            {
                AdvanceOrdered(now, out);
            }
            return true;
        }
    } // namespace Wire
} // namespace Marconi
