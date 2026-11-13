// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What the datagram channel measures about itself (loss, round-trip time, jitter, what was dropped and why), the
// text the status bar shows, and the hint a video encoder can use to adapt its bitrate

#pragma once

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Wire
    {
        // Microseconds on whatever clock the caller drives the channel with (steady_clock in the real thing, a
        // number the test advances in the unit tests).
        using Micros = int64_t;

        enum class LinkState : uint8_t
        {
            Idle,     // not started
            Probing,  // sending Hello (client) or waiting for it (server)
            Active,   // datagrams are getting through both ways
            FellBack  // UDP isn't working: messages go over the control channel; retried after a back-off
        };

        struct DatagramStats
        {
            LinkState state = LinkState::Idle;

            uint64_t packetsSent = 0;
            uint64_t packetsReceived = 0; // authenticated, in-window
            uint64_t bytesSent = 0;
            uint64_t bytesReceived = 0;

            // Refused datagrams, by reason (none of them is ever answered)
            uint64_t malformed = 0;      // too short, wrong format byte, bad inner layout
            uint64_t wrongSession = 0;   // another session id
            uint64_t authFailures = 0;   // didn't authenticate (tampered, wrong key)
            uint64_t replaysRejected = 0; // a packet number seen before, or too old

            uint64_t messagesDelivered = 0;
            uint64_t messagesDeadlineDropped = 0; // incomplete when their deadline passed
            uint64_t messagesStale = 0;           // arrived after a newer one was already delivered
            uint64_t gapsReported = 0;            // messages the ordered channels told the receiver were lost
            uint64_t messagesOverTcp = 0;         // sent or received on the control channel instead
            uint32_t fallbacks = 0;               // times the channel gave up on UDP

            // Loss in the direction I receive, over the last few seconds, 0 to 100.
            double lossPercent = 0;
            // Loss the peer reports for what I send (how my video fared); valid when havePeerReport.
            double peerLossPercent = 0;
            bool havePeerReport = false;

            bool haveRtt = false;
            Micros rttUs = 0;    // smoothed
            Micros minRttUs = 0; // lowest of the recent samples: the path with nothing queued
            Micros jitterUs = 0; // smoothed round-trip variation
        };

        // Smoothed round-trip time and its variation as TCP does it (RFC 6298), plus the minimum of the last 32
        // samples.
        class RttEstimator
        {
        public:
            void Sample(Micros rttUs);
            bool HasSample() const { return m_count > 0; }
            Micros Smoothed() const { return m_smoothed; }
            Micros Variation() const { return m_variation; }
            Micros Minimum() const;

        private:
            Micros m_smoothed = 0;
            Micros m_variation = 0;
            Micros m_recent[32] = {};
            uint32_t m_count = 0;
        };

        enum class BitrateAdvice : uint8_t
        {
            Decrease,
            Hold,
            Increase
        };

        struct BitrateHint
        {
            BitrateAdvice advice = BitrateAdvice::Hold;
            int scalePercent = 100; // multiply the current bitrate by this (70 to 110)
        };

        // From the loss the peer reports for what we send (our own measurement of what we receive if it has not
        // reported yet) and how far the round-trip time has risen above its minimum. A starting point for the
        // video agent's controller, not a tuned one: the thresholds are guesses until measured on real links.
        BitrateHint ComputeBitrateHint(const DatagramStats& stats);

        // For the status bar: "UDP", "UDP, 3% loss", "UDP starting", "over TCP" (UDP is blocked or lost), or empty
        // when the channel is not in use.
        std::string DatagramStatusText(const DatagramStats& stats);
    } // namespace Wire
} // namespace Marconi
