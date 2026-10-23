// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finds which remote-desktop services a host offers by connecting to ports and reading their greetings

#include <MarconiCore/Net/Probe.h>

#include <MarconiCore/Net/Socket.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            // X.224 Connection Request carrying an RDP Negotiation Request for TLS or CredSSP ([MS-RDPBCGR] 2.2.1.1).
            constexpr uint8_t kRdpConnectionRequest[] = {0x03, 0x00, 0x00, 0x13, 0x0e, 0xe0, 0x00, 0x00, 0x00, 0x00,
                                                         0x00, 0x01, 0x00, 0x08, 0x00, 0x03, 0x00, 0x00, 0x00};
            constexpr size_t kMaxGreeting = 255;

            using Clock = std::chrono::steady_clock;

            int RemainingMs(Clock::time_point deadline)
            {
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                return left > 0 ? static_cast<int>(left) : 0;
            }

            bool Cancelled(const std::atomic<bool>* cancel)
            {
                return cancel != nullptr && cancel->load();
            }

            // Reads until a newline, `wanted` bytes, end of stream or the deadline, in slices so cancel is noticed.
            std::string ReadSome(Socket& socket, size_t wanted, bool untilNewline, Clock::time_point deadline,
                                 const std::atomic<bool>* cancel)
            {
                std::string data;
                char buffer[kMaxGreeting];
                while (data.size() < wanted && !Cancelled(cancel))
                {
                    int left = RemainingMs(deadline);
                    if (left == 0)
                    {
                        break;
                    }
                    if (!socket.WaitReadable(std::min(left, 100)))
                    {
                        continue;
                    }
                    ptrdiff_t got = socket.Receive(buffer, std::min(sizeof(buffer), wanted - data.size()));
                    if (got <= 0)
                    {
                        break;
                    }
                    data.append(buffer, static_cast<size_t>(got));
                    if (untilNewline && data.find('\n') != std::string::npos)
                    {
                        break;
                    }
                }
                return data;
            }

            std::string FirstLine(const std::string& text)
            {
                size_t end = text.find_first_of("\r\n");
                return text.substr(0, end);
            }

            // Connects `socket` to the target's port, within the timeout. False (with `error`) if nothing accepted.
            using ConnectFunction = std::function<bool(Socket& socket, uint16_t port, std::string& error)>;

            ProbeResult ProbeOne(const ConnectFunction& connect, const ProbeTarget& target, int timeoutMs,
                                 const std::atomic<bool>* cancel)
            {
                ProbeResult result;
                result.port = target.port;
                Socket socket;
                std::string error;
                if (!connect(socket, target.port, error))
                {
                    result.detail = error;
                    return result;
                }
                result.reachable = true;
                // From here, not from before connecting: a slow lookup mustn't leave a server no time to answer.
                Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

                if (target.expected != Protocol::Rdp)
                {
                    // VNC and SSH servers speak first. On an unknown port, give a silent server half the time
                    // before assuming it might be RDP (which waits for the client).
                    Clock::time_point greetingDeadline =
                        target.expected == Protocol::Auto
                            ? Clock::now() + std::chrono::milliseconds(RemainingMs(deadline) / 2)
                            : deadline;
                    std::string greeting = ReadSome(socket, kMaxGreeting, true, greetingDeadline, cancel);
                    if (!greeting.empty())
                    {
                        result.protocol = IdentifyGreeting(greeting);
                        result.detail =
                            result.protocol == Protocol::Auto ? "unrecognised greeting" : FirstLine(greeting);
                        return result;
                    }
                    if (target.expected != Protocol::Auto)
                    {
                        result.detail = "no greeting";
                        return result;
                    }
                }

                if (!socket.SendAll(kRdpConnectionRequest, sizeof(kRdpConnectionRequest)))
                {
                    result.detail = "closed the connection";
                    return result;
                }
                std::string reply = ReadSome(socket, 19, false, deadline, cancel);
                if (IsRdpConnectionConfirm(reinterpret_cast<const uint8_t*>(reply.data()), reply.size()))
                {
                    result.protocol = Protocol::Rdp;
                    result.detail = "RDP";
                }
                else
                {
                    result.detail = reply.empty() ? "no greeting" : "unrecognised greeting";
                }
                return result;
            }
        } // namespace

        std::vector<ProbeTarget> DefaultProbeTargets(const Address& address)
        {
            if (address.protocol != Protocol::Auto)
            {
                return {{address.port != 0 ? address.port : DefaultPort(address.protocol), address.protocol}};
            }
            if (address.port != 0)
            {
                return {{address.port, Protocol::Auto}};
            }
            std::vector<ProbeTarget> targets = {{DefaultPort(Protocol::Marconi), Protocol::Marconi},
                                                {DefaultPort(Protocol::Vnc), Protocol::Vnc},
                                                {DefaultPort(Protocol::Rdp), Protocol::Rdp},
                                                {DefaultPort(Protocol::Ssh), Protocol::Ssh}};
            // VNC servers for a second (third, ...) desktop listen on the following ports. They come last, so on a
            // tie the usual port wins and these are the fallbacks.
            for (uint16_t display = 1; display <= kExtraVncDisplays; ++display)
            {
                targets.push_back({static_cast<uint16_t>(DefaultPort(Protocol::Vnc) + display), Protocol::Vnc});
            }
            return targets;
        }

        namespace
        {
            std::vector<ProbeResult> ProbeAll(const ConnectFunction& connect, const std::vector<ProbeTarget>& targets,
                                              int timeoutMs, const std::atomic<bool>* cancel)
            {
                std::vector<ProbeResult> results(targets.size());
                std::vector<std::thread> threads;
                for (size_t i = 0; i < targets.size(); ++i)
                {
                    threads.emplace_back([&, i]() { results[i] = ProbeOne(connect, targets[i], timeoutMs, cancel); });
                }
                for (std::thread& thread : threads)
                {
                    thread.join();
                }
                return results;
            }
        } // namespace

        std::vector<ProbeResult> Probe(const std::string& host, const std::vector<ProbeTarget>& targets, int timeoutMs,
                                       const std::atomic<bool>* cancel)
        {
            ConnectFunction connect = [&](Socket& socket, uint16_t port, std::string& error) {
                return socket.Connect(host, port, timeoutMs, error, cancel);
            };
            return ProbeAll(connect, targets, timeoutMs, cancel);
        }

        std::vector<ProbeResult> ProbeAddresses(const std::vector<std::string>& numericHosts,
                                                const std::vector<ProbeTarget>& targets, int timeoutMs,
                                                const std::atomic<bool>* cancel)
        {
            ConnectFunction connect = [&](Socket& socket, uint16_t port, std::string& error) {
                return socket.ConnectAddresses(numericHosts, port, timeoutMs, error, cancel);
            };
            return ProbeAll(connect, targets, timeoutMs, cancel);
        }

        Protocol IdentifyGreeting(const std::string& greeting)
        {
            // A Marconi server sends its four magic bytes before anything else (then waits for the TLS handshake).
            if (greeting.compare(0, 4, "MRCN") == 0)
            {
                return Protocol::Marconi;
            }
            if (greeting.compare(0, 4, "RFB ") == 0)
            {
                return Protocol::Vnc;
            }
            if (greeting.compare(0, 4, "SSH-") == 0)
            {
                return Protocol::Ssh;
            }
            return Protocol::Auto;
        }

        bool IsRdpConnectionConfirm(const uint8_t* data, size_t size)
        {
            // TPKT version 3, then an X.224 header whose code (high nibble) is Connection Confirm (0xD).
            return size >= 7 && data[0] == 0x03 && data[1] == 0x00 && (data[5] & 0xF0) == 0xD0;
        }
    } // namespace Net
} // namespace Marconi
