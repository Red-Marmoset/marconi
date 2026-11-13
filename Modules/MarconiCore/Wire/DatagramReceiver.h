// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reassembly of datagram-channel messages from fragments, per channel, with the delivery rules of each channel
// kind: latest-wins (state: a newer message replaces an older one, nothing waits) and ordered (video: messages come
// out in order, a lost one is reported as a gap once its deadline passes, a late one is dropped)

#pragma once

#include <MarconiCore/Wire/DatagramStats.h>
#include <MarconiCore/Wire/WireStream.h>

#include <cstdint>
#include <map>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        enum class ChannelKind : uint8_t
        {
            LatestWins, // unreliable, unordered: only the newest message matters (pointer, gamepad state, cursor)
            Ordered,    // unreliable, in order, gaps reported (video)
            Reliable    // always on the control channel, in order, nothing lost
        };

        struct ChannelSpec
        {
            uint8_t id = 0;
            ChannelKind kind = ChannelKind::LatestWins;
        };

        // One message handed to the application.
        struct Delivery
        {
            uint8_t channel = 0;
            uint32_t messageId = 0;
            Bytes payload;
            bool overTcp = false; // it came on the control channel, not as datagrams
        };

        // Messages that will never arrive: `count` messages from `firstMessageId` on, on an ordered channel. The
        // receiver of a video stream asks for a keyframe.
        struct Gap
        {
            uint8_t channel = 0;
            uint32_t firstMessageId = 0;
            uint32_t count = 0;
        };

        // What a receive or tick produced, in the order it happened: a gap comes before the message after it.
        struct ReceiveEvents
        {
            struct Event
            {
                bool isGap = false;
                Delivery delivery;
                Gap gap;
            };
            std::vector<Event> items;

            void AddDelivery(Delivery&& delivery)
            {
                Event event;
                event.delivery = std::move(delivery);
                items.push_back(std::move(event));
            }
            void AddGap(const Gap& gap)
            {
                Event event;
                event.isGap = true;
                event.gap = gap;
                items.push_back(std::move(event));
            }
            void Append(ReceiveEvents&& other)
            {
                for (Event& event : other.items)
                {
                    items.push_back(std::move(event));
                }
            }
        };

        struct ReceiverLimits
        {
            Micros deadlineUs = 100000;               // an incomplete message is dropped this long after its first fragment
            size_t maxMessageBytes = 8 * 1024 * 1024; // larger messages are refused
            size_t maxPendingMessages = 64;           // partial messages held at once
        };

        class ChannelReceiver
        {
        public:
            ChannelReceiver(uint8_t channel, ChannelKind kind, const ReceiverLimits& limits);

            // One authenticated fragment. Returns false if it was malformed (index past the count, a count that
            // changed, a message past the size limit).
            bool OnFragment(uint32_t messageId, uint16_t index, uint16_t count, const uint8_t* data, size_t size,
                            Micros now, ReceiveEvents& out);
            // Expires incomplete messages and, on ordered channels, gives up on missing ones.
            void Tick(Micros now, ReceiveEvents& out);
            // A message arrived on the control channel (the caller delivers it); catch up so older datagram copies
            // are not delivered after it. False if it is older than what was already delivered (the caller drops it).
            bool OnOutOfBand(uint32_t messageId, Micros now, ReceiveEvents& out);

            // Starts counting from `nextMessage` (the 64-bit message number). For tests of id wrap-around.
            void SkipTo(uint64_t nextMessage) { m_next = nextMessage; }

            uint64_t DeadlineDropped() const { return m_deadlineDropped; }
            uint64_t Stale() const { return m_stale; }
            uint64_t GapMessages() const { return m_gapMessages; }
            uint64_t Delivered() const { return m_delivered; }

        private:
            struct Partial
            {
                uint16_t count = 0;
                uint16_t have = 0;
                size_t bytes = 0;
                Micros firstSeen = 0;
                std::vector<Bytes> pieces;
                std::vector<uint8_t> present;
            };

            int64_t Unwrap(uint32_t messageId) const;
            bool Add(Partial& partial, uint16_t index, uint16_t count, const uint8_t* data, size_t size, Micros now);
            static Bytes Join(const Partial& partial);
            void Deliver(uint64_t id, Partial& partial, ReceiveEvents& out);
            void AdvanceOrdered(Micros now, ReceiveEvents& out);

            uint8_t m_channel;
            ChannelKind m_kind;
            ReceiverLimits m_limits;
            std::map<uint64_t, Partial> m_partials;
            uint64_t m_next = 0; // ordered: the next message to deliver; latest-wins: one past the last delivered
            Micros m_waitingSince = -1;
            uint64_t m_deadlineDropped = 0;
            uint64_t m_stale = 0;
            uint64_t m_gapMessages = 0;
            uint64_t m_delivered = 0;
        };
    } // namespace Wire
} // namespace Marconi
