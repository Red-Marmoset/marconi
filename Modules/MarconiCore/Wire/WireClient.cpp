// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol client

#include <MarconiCore/Wire/WireClient.h>

#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Wire/WireCompress.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            // Carries the protocol over the TLS stream once the handshake succeeded.
            class TlsStreamTransport : public Transport
            {
            public:
                explicit TlsStreamTransport(Net::TlsStream& tls) : m_tls(tls) {}
                ptrdiff_t Read(void* buffer, size_t length) override { return m_tls.Read(buffer, length); }
                bool Write(const void* data, size_t length) override { return m_tls.Write(data, length); }

            private:
                Net::TlsStream& m_tls;
            };

            // The whole desktop's size is the box that holds every display.
            void DesktopSize(const std::vector<Display>& displays, int& width, int& height)
            {
                width = 0;
                height = 0;
                for (const Display& display : displays)
                {
                    width = std::max(width, display.x + display.width);
                    height = std::max(height, display.y + display.height);
                }
            }

            std::vector<Rfb::Screen> ToScreens(const std::vector<Display>& displays)
            {
                std::vector<Rfb::Screen> screens;
                screens.reserve(displays.size());
                for (const Display& display : displays)
                {
                    Rfb::Screen screen;
                    screen.id = display.id;
                    screen.x = display.x;
                    screen.y = display.y;
                    screen.width = display.width;
                    screen.height = display.height;
                    screens.push_back(screen);
                }
                return screens;
            }
        } // namespace

        Client::Client(SessionListener& listener) : m_listener(listener)
        {
        }

        Client::~Client()
        {
            Disconnect();
        }

        void Client::Connect(const ClientOptions& options)
        {
            Disconnect();
            m_stopping = false;
            m_thread = std::thread([this, options]() { Run(options); });
        }

        void Client::Disconnect()
        {
            m_stopping = true;
            m_socket.Shutdown();
            if (m_thread.joinable())
            {
                if (m_thread.get_id() == std::this_thread::get_id())
                {
                    m_thread.detach(); // called from a listener callback on the worker thread: can't join itself
                }
                else
                {
                    m_thread.join();
                }
            }
            m_connected = false;
        }

        bool Client::Send(const Bytes& framed)
        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            if (!m_transport)
            {
                return false;
            }
            return WriteMessage(*m_transport, framed);
        }

        void Client::MarkInput()
        {
            m_lastInputTime = std::chrono::steady_clock::now().time_since_epoch().count();
        }

        void Client::Run(ClientOptions options)
        {
            DisconnectInfo info;
            std::string error;
            if (!m_socket.Connect(options.host, options.port, options.connectTimeoutMs, error, &m_stopping))
            {
                info.error = error;
                m_listener.OnDisconnected(info);
                return;
            }
            // The server sends the four magic bytes in the clear, before the TLS handshake, so read and check them
            // straight off the socket first.
            uint8_t magic[sizeof(kMagic)] = {};
            size_t got = 0;
            while (got < sizeof(magic))
            {
                ptrdiff_t n = m_socket.Receive(magic + got, sizeof(magic) - got);
                if (n <= 0)
                {
                    break;
                }
                got += static_cast<size_t>(n);
            }
            if (got != sizeof(magic) || std::memcmp(magic, kMagic, sizeof(magic)) != 0)
            {
                info.error = m_stopping ? "" : "This doesn't look like a Marconi server";
                m_listener.OnDisconnected(info);
                return;
            }

            // Upgrade the connection to TLS. The server's certificate is self-signed, so the trust decision is
            // asked unless its fingerprint is the one the user accepted before (pinned).
            Net::TlsOptions tlsOptions;
            tlsOptions.hostName = options.host;
            std::vector<uint8_t> pinned = options.trustedCertificateSha256;
            tlsOptions.trustDecision = [this, pinned](const Net::TlsCertificate& certificate) {
                if (!pinned.empty() && certificate.sha256 == pinned)
                {
                    return true;
                }
                return m_listener.OnUntrustedCertificate(certificate);
            };
            std::unique_ptr<Net::TlsStream> tls = Net::CreateTlsStream(m_socket, tlsOptions);
            if (!tls)
            {
                info.error = "This build can't make encrypted connections";
                m_listener.OnDisconnected(info);
                return;
            }
            if (!tls->Handshake(error))
            {
                info.error = m_stopping ? "" : error;
                m_listener.OnDisconnected(info);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_nameMutex);
                m_encryptionName = tls->GetProtocolName();
            }
            m_encrypted = true;
            {
                std::lock_guard<std::mutex> lock(m_writeMutex);
                m_tls = std::move(tls);
                m_transport = std::make_unique<TlsStreamTransport>(*m_tls);
            }
            MessageReader reader(*m_transport);
            try
            {
                if (!Handshake(reader, options, info))
                {
                    std::lock_guard<std::mutex> lock(m_writeMutex);
                    m_transport.reset();
                    m_tls.reset();
                    m_connected = false;
                    m_encrypted = false;
                    m_listener.OnDisconnected(info);
                    return;
                }
                while (!m_stopping)
                {
                    Bytes bytes = reader.Next();
                    if (m_inFrame)
                    {
                        m_frame.bytes += bytes.size();
                    }
                    Reader body(bytes);
                    Type type = ReadType(body);
                    switch (type)
                    {
                    case Type::FrameBegin:
                    {
                        FrameBegin begin = FrameBegin::Decode(body);
                        m_frameRects.clear();
                        m_frame = FrameTiming();
                        m_frame.frameId = begin.frameId;
                        m_frame.beganMicros = static_cast<int64_t>(NowMicros());
                        m_frame.bytes = bytes.size();
                        m_frame.decodeMicros = 0;
                        m_inFrame = true;
                        break;
                    }
                    case Type::Still:
                    {
                        uint64_t started = NowMicros();
                        HandleStill(Still::Decode(body));
                        m_frame.decodeMicros += static_cast<int64_t>(NowMicros() - started);
                        break;
                    }
                    case Type::Move:
                        HandleMove(Move::Decode(body));
                        break;
                    case Type::FrameEnd:
                        // Timed before the listener hears of it, so the paint it causes finds the update.
                        FinishFrame(FrameEnd::Decode(body));
                        ++m_statUpdates;
                        m_listener.OnFramebufferUpdated(m_frameRects);
                        break;
                    case Type::CursorShape:
                    {
                        Wire::CursorShape shape = Wire::CursorShape::Decode(body);
                        Marconi::CursorShape cursor;
                        cursor.width = shape.width;
                        cursor.height = shape.height;
                        cursor.hotspotX = shape.hotspotX;
                        cursor.hotspotY = shape.hotspotY;
                        cursor.argb.resize(shape.argb.size() / 4);
                        for (size_t i = 0; i < cursor.argb.size(); ++i)
                        {
                            const uint8_t* p = shape.argb.data() + i * 4;
                            cursor.argb[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                             (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
                        }
                        m_listener.OnCursorShape(cursor);
                        break;
                    }
                    case Type::Displays:
                        ApplyDisplays(Displays::Decode(body).displays, false);
                        break;
                    case Type::Pong:
                        HandlePong(Pong::Decode(body));
                        break;
                    case Type::Bye:
                    {
                        Bye bye = Bye::Decode(body);
                        info.error = bye.reason;
                        info.wasConnected = true;
                        m_stopping = true;
                        break;
                    }
                    default:
                        break; // a message this build doesn't handle yet; its length let us skip it
                    }
                }
            }
            catch (const WireError& wireError)
            {
                if (!m_stopping)
                {
                    info.error = wireError.what();
                    info.wasConnected = true;
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_writeMutex);
                m_transport.reset();
                m_tls.reset();
            }
            m_connected = false;
            m_encrypted = false;
            m_listener.OnDisconnected(info);
        }

        bool Client::Handshake(MessageReader& reader, ClientOptions& options, DisconnectInfo& info)
        {
            Hello hello;
            hello.version = kProtocolVersion;
            hello.capabilities = options.capabilities;
            hello.clientName = options.clientName;
            if (!Send(hello.Encode()))
            {
                info.error = "Could not send the greeting";
                return false;
            }

            Bytes welcomeBytes = reader.Next();
            Reader welcomeReader(welcomeBytes);
            if (ReadType(welcomeReader) != Type::Welcome)
            {
                info.error = "The server didn't answer with a welcome";
                return false;
            }
            Welcome welcome = Welcome::Decode(welcomeReader);

            Login login;
            login.password = options.password;
            if (!Send(login.Encode()))
            {
                info.error = "Could not send the login";
                return false;
            }
            Bytes resultBytes = reader.Next();
            Reader resultReader(resultBytes);
            if (ReadType(resultReader) != Type::LoginResult)
            {
                info.error = "The server didn't answer the login";
                return false;
            }
            LoginResult result = LoginResult::Decode(resultReader);
            if (result.status != LoginResult::Status::Ok)
            {
                info.authenticationFailed = result.status == LoginResult::Status::WrongPassword ||
                                            result.status == LoginResult::Status::LockedOut;
                info.error = result.status == LoginResult::Status::WrongPassword ? "The password was not accepted"
                             : result.status == LoginResult::Status::LockedOut
                                 ? "Too many wrong passwords; the server locked login for a while"
                                 : "The server has no password set, so it refused the connection";
                return false;
            }

            m_timestamps = (welcome.capabilities & Capability::Timestamps) != 0;
            m_clock.Reset();
            m_clockKnown = false;
            m_pingSentMicros = 0;
            m_roundTripMs = -1;
            m_lastUpdateMicros = 0;
            m_inFrame = false;
            m_performance.Reset();
            m_lastSummaryMicros = static_cast<int64_t>(NowMicros());
            m_lastPingMicros = 0;

            int width = 0;
            int height = 0;
            DesktopSize(welcome.displays, width, height);
            m_framebuffer.Resize(width, height);
            ApplyDisplays(welcome.displays, true);

            Rfb::ServerInit serverInit;
            serverInit.width = width;
            serverInit.height = height;
            serverInit.name = welcome.serverName;
            m_connected = true;
            m_listener.OnConnected(serverInit);
            LOG_INFO("Wire", "Connected to '%s' (%s), desktop %dx%d", welcome.serverName.c_str(),
                     welcome.serverVersion.c_str(), width, height);
            return true;
        }

        void Client::ApplyDisplays(const std::vector<Display>& displays, bool initial)
        {
            std::vector<Rfb::Screen> screens = ToScreens(displays);
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_screens = screens;
            }
            m_supportsResize = false; // set when the server says it can resize (ResizeDisplay capability), later
            if (!initial)
            {
                int width = 0;
                int height = 0;
                DesktopSize(displays, width, height);
                if (width != m_framebuffer.GetWidth() || height != m_framebuffer.GetHeight())
                {
                    m_framebuffer.Resize(width, height);
                    m_listener.OnDesktopResized(width, height);
                }
            }
            m_listener.OnScreenLayout(screens);
        }

        void Client::HandleStill(const Still& still)
        {
            const Rfb::Rect rect{still.rect.x, still.rect.y, still.rect.width, still.rect.height};
            if (rect.width <= 0 || rect.height <= 0)
            {
                return;
            }
            const size_t pixelCount = static_cast<size_t>(rect.width) * rect.height;
            const size_t byteCount = pixelCount * 4;

            // Decode the pixels into a scratch buffer first (never hold the framebuffer lock while decompressing).
            std::vector<uint32_t> pixels(pixelCount);
            if (still.compression == Still::Compression::None)
            {
                if (still.data.size() != byteCount)
                {
                    throw WireError("a still rectangle's size doesn't match its pixels");
                }
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    const uint8_t* p = still.data.data() + i * 4;
                    pixels[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16);
                }
            }
            else // Zlib
            {
                Bytes raw;
                if (!ZlibDecompress(still.data, raw, byteCount))
                {
                    throw WireError("a still rectangle didn't decompress to its size");
                }
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    const uint8_t* p = raw.data() + i * 4;
                    pixels[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                if (!m_framebuffer.Contains(rect))
                {
                    return; // a rectangle outside the desktop (a resize we haven't applied): drop it
                }
                m_framebuffer.PutPixels(rect, pixels.data(), static_cast<size_t>(rect.width));
            }
            ++m_statRectangles;
            m_frameRects.push_back(rect);
        }

        void Client::HandleMove(const Move& move)
        {
            const Rfb::Rect dest{move.destination.x, move.destination.y, move.destination.width,
                                 move.destination.height};
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                if (!m_framebuffer.Contains(dest))
                {
                    return;
                }
                m_framebuffer.CopyRect(dest, move.sourceX, move.sourceY);
            }
            m_frameRects.push_back(dest);
        }

        void Client::SendPointerEvent(int x, int y, uint8_t buttonMask)
        {
            MarkInput();
            Pointer pointer;
            pointer.x = x;
            pointer.y = y;
            pointer.buttons = buttonMask;
            ++m_statPointerEvents;
            Send(pointer.Encode());
        }

        void Client::SendKeyEvent(uint32_t keysym, bool down)
        {
            MarkInput();
            Key key;
            key.keysym = keysym;
            key.scancode = 0; // the viewer sends keysyms today; scancodes arrive with the game-input work
            key.down = down;
            ++m_statKeyEvents;
            Send(key.Encode());
        }

        void Client::SendClientCutText(const std::string& /*latin1Text*/)
        {
            // Clipboard over the Marconi protocol is Phase 6; nothing to send yet.
        }

        bool Client::OfferClipboard(const Rfb::ClipboardContent& /*content*/)
        {
            return false; // not active yet
        }

        void Client::RequestFullUpdate()
        {
            // The server pushes the picture as it changes, so there is no update to ask for. A later refresh
            // message can force a full repaint; for now this is a no-op.
        }

        std::vector<Rfb::Screen> Client::GetScreens() const
        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
            return m_screens;
        }

        void Client::RequestDesktopSize(int width, int height)
        {
            if (!m_supportsResize.load())
            {
                return;
            }
            SetDisplayMode mode;
            mode.displayId = 0;
            mode.width = width;
            mode.height = height;
            Send(mode.Encode());
        }

        void Client::SetQuality(int /*compressLevel*/, int /*qualityLevel*/)
        {
            // Picture quality maps to StreamConfig once video exists; for lossless stills there is nothing to set.
        }

        std::string Client::GetEncryptionName() const
        {
            std::lock_guard<std::mutex> lock(m_nameMutex);
            return m_encryptionName;
        }

        std::chrono::steady_clock::duration Client::GetTimeSinceInput() const
        {
            std::chrono::steady_clock::time_point last{
                std::chrono::steady_clock::duration{m_lastInputTime.load()}};
            return std::chrono::steady_clock::now() - last;
        }

        void Client::SendPing()
        {
            // Any thread. Only one ping at a time: a server that has gone quiet would otherwise get one every tick,
            // and the first unanswered one is the time that matters.
            if (!m_connected)
            {
                return;
            }
            // Claim the one outstanding ping atomically: the UI thread and the connection thread both ping.
            uint64_t idle = 0;
            if (!m_pingSentMicros.compare_exchange_strong(idle, NowMicros()))
            {
                return;
            }
            Ping ping;
            ping.token = ++m_pingToken;
            if (!Send(ping.Encode()))
            {
                m_pingSentMicros = 0;
            }
        }

        void Client::HandlePong(const Pong& pong)
        {
            const uint64_t received = NowMicros();
            const uint64_t sent = m_pingSentMicros.load();
            if (sent == 0 || pong.token != m_pingToken.load())
            {
                return; // not the ping we're waiting for (a stale one, or a server answering something else)
            }
            m_pingSentMicros = 0;
            m_roundTripMs = static_cast<int>((received - sent) / 1000);
            if (pong.hasTimes)
            {
                m_clock.AddSample(sent, pong.receivedMicros, pong.answeredMicros, received);
                m_clockKnown = m_clock.HasEstimate();
            }
        }

        ConnectionHealth Client::GetHealth() const
        {
            ConnectionHealth health;
            const uint64_t now = NowMicros();
            health.roundTripMs = m_roundTripMs.load();
            const uint64_t sent = m_pingSentMicros.load();
            if (sent != 0 && now >= sent)
            {
                health.waitingMs = static_cast<int>((now - sent) / 1000);
            }
            const uint64_t last = m_lastUpdateMicros.load();
            if (last != 0 && now >= last)
            {
                health.sinceUpdateMs = static_cast<int>((now - last) / 1000);
            }
            return health;
        }

        void Client::FinishFrame(const FrameEnd& end)
        {
            const int64_t now = static_cast<int64_t>(NowMicros());
            m_lastUpdateMicros = static_cast<uint64_t>(now);
            if (!m_inFrame)
            {
                return; // a FrameEnd without its FrameBegin: nothing to time
            }
            m_inFrame = false;
            m_frame.receivedMicros = now;
            // The server's times only mean something here once the two clocks have been compared.
            if (end.hasTimes && m_clockKnown)
            {
                m_frame.captureMicros = m_clock.ToLocalMicros(end.captureMicros);
                m_frame.sentMicros = m_clock.ToLocalMicros(end.sentMicros);
                m_frame.compressMicros = end.compressMicros;
            }
            m_performance.FrameReceived(m_frame);

            if (m_statsLogging)
            {
                auto ms = [](int64_t micros) { return micros < 0 ? -1.0 : static_cast<double>(micros) / 1000.0; };
                LOG_INFO("Stats",
                         "frame %llu: %llu bytes; server %.1f ms (compress %.1f), network %.1f ms, decode %.1f ms, "
                         "capture to received %.1f ms",
                         static_cast<unsigned long long>(m_frame.frameId),
                         static_cast<unsigned long long>(m_frame.bytes),
                         m_frame.captureMicros >= 0 ? ms(m_frame.sentMicros - m_frame.captureMicros) : -1.0,
                         ms(m_frame.compressMicros),
                         m_frame.sentMicros >= 0 ? ms(now - m_frame.sentMicros) : -1.0, ms(m_frame.decodeMicros),
                         m_frame.captureMicros >= 0 ? ms(now - m_frame.captureMicros) : -1.0);
                LogSummaryIfDue(now);
            }

            // Keep the clock comparison fresh while pictures flow: a ping a second at most, from this thread, so
            // the viewer's slower status-bar ping isn't the only source.
            constexpr int64_t kPingMicros = 1000 * 1000;
            if (m_timestamps && m_pingSentMicros.load() == 0 && now - m_lastPingMicros >= kPingMicros)
            {
                m_lastPingMicros = now;
                SendPing();
            }
        }

        void Client::LogSummaryIfDue(int64_t now)
        {
            constexpr int64_t kSummaryMicros = 10 * 1000 * 1000;
            if (now - m_lastSummaryMicros < kSummaryMicros)
            {
                return;
            }
            m_lastSummaryMicros = now;
            PerformanceSnapshot s = m_performance.Summarise(now, kSummaryMicros);
            LOG_INFO("Stats",
                     "last 10 s: %d updates (%.1f a second), %.1f shown a second, %.0f kbit/s, round trip %d ms; "
                     "medians: server %.1f ms (compress %.1f), network %.1f, decode %.1f, shown after %.1f; "
                     "capture to shown %.1f ms (99th percentile %.1f)",
                     s.frames, s.framesPerSecond, s.shownPerSecond, s.kilobitsPerSecond, m_roundTripMs.load(),
                     s.serverMs, s.compressMs, s.networkMs, s.decodeMs, s.showMs, s.pipelineMs, s.pipelineP99Ms);
        }

        void Client::NotePainted()
        {
            m_performance.Shown(static_cast<int64_t>(NowMicros()));
        }

        bool Client::GetPerformance(int64_t windowMicros, PerformanceSnapshot& out) const
        {
            out = m_performance.Summarise(static_cast<int64_t>(NowMicros()), windowMicros);
            out.roundTripMs = m_roundTripMs.load();
            return true;
        }

        TrafficStats Client::TakeTrafficStats()
        {
            TrafficStats stats;
            stats.updates = m_statUpdates.exchange(0);
            stats.rectangles = m_statRectangles.exchange(0);
            stats.pointerEvents = m_statPointerEvents.exchange(0);
            stats.keyEvents = m_statKeyEvents.exchange(0);
            return stats;
        }
    } // namespace Wire
} // namespace Marconi
