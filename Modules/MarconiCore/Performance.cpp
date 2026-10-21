// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Per-update timing and its summary

#include <MarconiCore/Performance.h>

#include <algorithm>
#include <cmath>

namespace Marconi
{
    namespace
    {
        // Long enough for the 10-second --stats summary; older entries are dropped as new ones arrive.
        constexpr int64_t kKeepMicros = 15 * 1000 * 1000;
        constexpr size_t kMaxFrames = 4096;

        double Ms(int64_t micros)
        {
            return static_cast<double>(micros) / 1000.0;
        }
    } // namespace

    double Percentile(std::vector<double>& values, double q)
    {
        if (values.empty())
        {
            return -1;
        }
        std::sort(values.begin(), values.end());
        q = std::min(1.0, std::max(0.0, q));
        size_t index = static_cast<size_t>(std::ceil(q * static_cast<double>(values.size())));
        index = index == 0 ? 0 : index - 1;
        return values[std::min(index, values.size() - 1)];
    }

    void PerformanceMonitor::Reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.clear();
        m_paints.clear();
    }

    void PerformanceMonitor::FrameReceived(const FrameTiming& timing)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.push_back(timing);
        while (!m_frames.empty() &&
               (m_frames.size() > kMaxFrames || m_frames.front().receivedMicros < timing.receivedMicros - kKeepMicros))
        {
            m_frames.pop_front();
        }
    }

    std::vector<FrameTiming> PerformanceMonitor::Shown(int64_t nowMicros)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<FrameTiming> shown;
        // Walk back from the newest: everything not yet shown was shown by this paint.
        for (auto it = m_frames.rbegin(); it != m_frames.rend() && it->shownMicros < 0; ++it)
        {
            it->shownMicros = nowMicros;
            shown.push_back(*it);
        }
        if (!shown.empty())
        {
            std::reverse(shown.begin(), shown.end());
            m_paints.push_back(nowMicros);
            while (!m_paints.empty() && m_paints.front() < nowMicros - kKeepMicros)
            {
                m_paints.pop_front();
            }
        }
        return shown;
    }

    PerformanceSnapshot PerformanceMonitor::Summarise(int64_t nowMicros, int64_t windowMicros) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PerformanceSnapshot snapshot;
        const int64_t from = nowMicros - windowMicros;
        const double seconds = static_cast<double>(windowMicros) / 1e6;
        std::vector<double> server, compress, network, decode, show, pipeline;
        uint64_t bytes = 0;
        for (const FrameTiming& frame : m_frames)
        {
            if (frame.receivedMicros < from || frame.receivedMicros > nowMicros)
            {
                continue;
            }
            ++snapshot.frames;
            bytes += frame.bytes;
            if (frame.decodeMicros >= 0)
            {
                decode.push_back(Ms(frame.decodeMicros));
            }
            if (frame.compressMicros >= 0)
            {
                compress.push_back(Ms(frame.compressMicros));
            }
            if (frame.captureMicros >= 0 && frame.sentMicros >= 0)
            {
                server.push_back(Ms(frame.sentMicros - frame.captureMicros));
            }
            if (frame.sentMicros >= 0)
            {
                // Clamp: with clocks compared to within a fraction of the round trip, a fast LAN can come out a
                // little below zero.
                network.push_back(std::max(0.0, Ms(frame.receivedMicros - frame.sentMicros)));
            }
            if (frame.shownMicros >= 0)
            {
                show.push_back(Ms(frame.shownMicros - frame.receivedMicros));
            }
            if (frame.captureMicros >= 0)
            {
                int64_t end = frame.shownMicros >= 0 ? frame.shownMicros : frame.receivedMicros;
                pipeline.push_back(std::max(0.0, Ms(end - frame.captureMicros)));
            }
        }
        int paints = 0;
        for (int64_t paint : m_paints)
        {
            if (paint >= from && paint <= nowMicros)
            {
                ++paints;
            }
        }
        if (seconds > 0)
        {
            snapshot.framesPerSecond = snapshot.frames / seconds;
            snapshot.shownPerSecond = paints / seconds;
            snapshot.kilobitsPerSecond = static_cast<double>(bytes) * 8.0 / 1000.0 / seconds;
        }
        snapshot.serverMs = Percentile(server, 0.5);
        snapshot.compressMs = Percentile(compress, 0.5);
        snapshot.networkMs = Percentile(network, 0.5);
        snapshot.decodeMs = Percentile(decode, 0.5);
        snapshot.showMs = Percentile(show, 0.5);
        std::vector<double> pipelineCopy = pipeline;
        snapshot.pipelineMs = Percentile(pipeline, 0.5);
        snapshot.pipelineP99Ms = Percentile(pipelineCopy, 0.99);
        return snapshot;
    }
} // namespace Marconi
