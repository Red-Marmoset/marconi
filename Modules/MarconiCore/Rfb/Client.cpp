// Copyright (c) 2026 Red Marmoset Studios Ltd.
// VNC client connection: owns the socket and a worker thread that runs the RFB protocol

#include <MarconiCore/Rfb/Client.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kMaxCutTextLength = 16 * 1024 * 1024;
            // Refuse absurd desktops from broken or hostile servers (16k x 16k is already 1 GiB of pixels).
            constexpr int kMaxDesktopSide = 16384;
            // How long a disconnect gives input already queued to go out (such as the keys that lock the other
            // computer's screen), when the user closes the connection.
            constexpr int kFlushOnDisconnectMs = 1000;

            // The queue's replace key for a pointer move: a move may stand in for the one before it only when both
            // hold the same buttons. Wheel "buttons" are clicks, so those are never merged.
            uint32_t PointerReplaceKey(uint8_t buttonMask)
            {
                return (buttonMask & 0x78) != 0 ? 0 : 0x100u | buttonMask;
            }

            void CheckDesktopSize(int width, int height)
            {
                if (width > kMaxDesktopSide || height > kMaxDesktopSide)
                {
                    throw RfbError("The server reported an implausibly large desktop (" + std::to_string(width) + " x " +
                                   std::to_string(height) + ")");
                }
            }

            Bytes BuildUpdateRequest(bool incremental, int width, int height)
            {
                OutBuffer message;
                message.U8(ClientMessage::FramebufferUpdateRequest)
                    .U8(incremental ? 1 : 0)
                    .U16(0)
                    .U16(0)
                    .U16(static_cast<uint16_t>(width))
                    .U16(static_cast<uint16_t>(height));
                return message.Data();
            }
        }

        Client::Client(ClientListener& listener)
            : m_listener(listener),
              m_clipboardChannel([this](const Bytes& message) { SendIfConnected(message); },
                                 [this](const ClipboardContent& content) { m_listener.OnServerClipboard(content); })
        {
        }

        Client::~Client()
        {
            Disconnect();
        }

        std::vector<int32_t> Client::GetDefaultEncodings()
        {
            std::vector<int32_t> encodings;
            for (int32_t encoding : {Encoding::Tight, Encoding::Zrle, Encoding::Hextile, Encoding::Rre,
                                     Encoding::CopyRect, Encoding::Raw})
            {
                if (CreateDecoder(encoding) != nullptr)
                {
                    encodings.push_back(encoding);
                }
            }
            return encodings;
        }

        void Client::Connect(const ConnectionOptions& options)
        {
            Disconnect();
            m_stopping = false;
            m_pingSentAt = 0;
            m_lastUpdateAt = 0;
            m_roundTripMs = -1;
            m_thread = std::thread(&Client::Run, this, options);
        }

        void Client::Disconnect()
        {
            if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
            {
                m_sendQueue.WaitUntilSent(kFlushOnDisconnectMs);
            }
            m_stopping = true;
            {
                std::lock_guard<std::mutex> lock(m_socketMutex);
                m_socket.Shutdown();
                if (m_tunnel)
                {
                    m_tunnel->Shutdown();
                }
            }
            if (m_thread.joinable())
            {
                if (m_thread.get_id() == std::this_thread::get_id())
                {
                    // Called from a listener callback: the thread finishes by itself once the socket is shut down.
                    m_thread.detach();
                    return;
                }
                m_sendQueue.Stop(); // the shutdown above gets a send waiting on a slow link out
                m_thread.join();
            }
        }

        bool Client::SendRaw(const Bytes& data)
        {
            if (m_tls)
            {
                return m_tls->Write(data.data(), data.size());
            }
            std::shared_ptr<Net::SshTunnel> tunnel = GetTunnel();
            return tunnel ? tunnel->Write(data.data(), data.size()) : m_socket.SendAll(data.data(), data.size());
        }

        std::shared_ptr<Net::SshTunnel> Client::GetTunnel() const
        {
            std::lock_guard<std::mutex> lock(m_socketMutex);
            return m_tunnel;
        }

        void Client::SetReceiveTimeout(int timeoutMs)
        {
            // Connection thread. The tunnel's timeout covers its own socket too.
            if (m_tunnel)
            {
                m_tunnel->SetReceiveTimeout(timeoutMs);
            }
            else
            {
                m_socket.SetReceiveTimeout(timeoutMs);
            }
        }

        bool Client::ConnectionTimedOut() const
        {
            return m_tunnel ? m_tunnel->TimedOut() : m_socket.TimedOut();
        }

        void Client::Send(const Bytes& data)
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            if (!SendRaw(data))
            {
                throw RfbError("Connection lost while sending");
            }
        }

        bool Client::SendFileTransferRequest(const Bytes& request)
        {
            // Called on the file panel's worker thread, which waits for the reply anyway; sending directly keeps a
            // large upload from piling up in memory faster than the link takes it.
            if (!m_connected || !m_fileTransfer)
            {
                return false;
            }
            std::lock_guard<std::mutex> lock(m_sendMutex);
            return SendRaw(request);
        }

        void Client::SetFileTransferHandler(std::function<void(const FileTransfer::Reply&)> handler)
        {
            std::lock_guard<std::mutex> lock(m_fileTransferMutex);
            m_fileTransferHandler = std::move(handler);
        }

        void Client::SendIfConnected(const Bytes& data, uint32_t replaceKey)
        {
            if (!m_connected)
            {
                return;
            }
            m_sendQueue.Post(
                [this, data]() {
                    std::lock_guard<std::mutex> lock(m_sendMutex);
                    // A failure here is reported by the connection thread when its next read fails.
                    SendRaw(data);
                },
                replaceKey);
        }

        ptrdiff_t Client::ReadFromConnection(void* buffer, size_t length)
        {
            // Connection thread only, which is also the thread that sets m_tls (in StartTls) and m_tunnel.
            ptrdiff_t got = m_tls      ? m_tls->Read(buffer, length)
                            : m_tunnel ? m_tunnel->Read(buffer, length)
                                       : m_socket.Receive(buffer, length);
            if (got < 0 && ConnectionTimedOut())
            {
                throw RfbError("The server stopped responding");
            }
            return got;
        }

        std::string Client::GetEncryptionName() const
        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
            return m_encryptionName;
        }

        void Client::StartTls(bool anonymous, const ConnectionOptions& options, const InStream& in)
        {
            if (in.GetBufferedCount() != 0)
            {
                throw RfbError("The server sent data before encryption started");
            }
            bool declined = false;
            Net::TlsOptions tlsOptions;
            tlsOptions.hostName = options.host;
            tlsOptions.anonymous = anonymous;
            tlsOptions.trustDecision = [this, &options, &declined](const Net::TlsCertificate& certificate) {
                const std::vector<uint8_t>& remembered = options.trustedCertificateSha256;
                if (!remembered.empty() && certificate.sha256 == remembered)
                {
                    return true;
                }
                bool trusted = m_listener.OnUntrustedCertificate(certificate);
                declined = !trusted;
                return trusted;
            };
            std::unique_ptr<Net::TlsStream> tls;
            if (options.createTlsStream)
            {
                tls = options.createTlsStream(m_socket, tlsOptions);
            }
            else if (m_tunnel)
            {
                // TLS inside the SSH tunnel: double encryption, but it's what a server that insists on TLS gets.
                std::shared_ptr<Net::SshTunnel> tunnel = m_tunnel;
                Net::ByteLink link;
                link.receive = [tunnel](void* buffer, size_t length) { return tunnel->Read(buffer, length); };
                link.sendAll = [tunnel](const void* data, size_t length) { return tunnel->Write(data, length); };
                tls = Net::CreateTlsStream(link, tlsOptions);
            }
            else
            {
                tls = Net::CreateTlsStream(m_socket, tlsOptions);
            }
            if (!tls)
            {
                throw RfbError("This build of Marconi can't make encrypted connections yet");
            }
            std::string error;
            if (!tls->Handshake(error))
            {
                if (declined)
                {
                    throw AuthenticationCancelled("The server's certificate was not accepted");
                }
                throw RfbError("Could not set up the encrypted connection: " + error);
            }
            std::string name = tls->GetProtocolName();
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_tls = std::move(tls);
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_encryptionName = m_encryptionName.empty() ? name : m_encryptionName + ", " + name; // "SSH, TLS 1.3"
            }
            m_encrypted = true;
        }

        void Client::SendPointerEvent(int x, int y, uint8_t buttonMask)
        {
            OutBuffer message;
            message.U8(ClientMessage::PointerEvent)
                .U8(buttonMask)
                .U16(static_cast<uint16_t>(std::max(x, 0)))
                .U16(static_cast<uint16_t>(std::max(y, 0)));
            SendIfConnected(message.Data(), PointerReplaceKey(buttonMask));
            ++m_statPointerEvents;
            m_lastInputTime = std::chrono::steady_clock::now().time_since_epoch().count();
        }

        void Client::SendKeyEvent(uint32_t keysym, bool down)
        {
            OutBuffer message;
            message.U8(ClientMessage::KeyEvent).U8(down ? 1 : 0).Padding(2).U32(keysym);
            SendIfConnected(message.Data());
            ++m_statKeyEvents;
            m_lastInputTime = std::chrono::steady_clock::now().time_since_epoch().count();
        }

        void Client::SendClientCutText(const std::string& latin1Text)
        {
            OutBuffer message;
            message.U8(ClientMessage::ClientCutText).Padding(3).U32(static_cast<uint32_t>(latin1Text.size())).Append(latin1Text);
            SendIfConnected(message.Data());
            LOG_INFO("Rfb", "Sent our clipboard to the server (%zu characters)", latin1Text.size());
        }

        std::chrono::steady_clock::duration Client::GetTimeSinceInput() const
        {
            std::chrono::steady_clock::duration last(m_lastInputTime.load());
            return std::chrono::steady_clock::now().time_since_epoch() - last;
        }

        TrafficStats Client::TakeTrafficStats()
        {
            TrafficStats stats;
            stats.updates = m_statUpdates.exchange(0);
            stats.rectangles = m_statRectangles.exchange(0);
            stats.requests = m_statRequests.exchange(0);
            stats.pointerEvents = m_statPointerEvents.exchange(0);
            stats.keyEvents = m_statKeyEvents.exchange(0);
            return stats;
        }

        void Client::SendPing()
        {
            // UI thread. A fresh ping only once the last was answered: a server that has gone quiet would otherwise
            // get one every tick, and the first unanswered one is the time that matters.
            if (!m_connected || m_pingSentAt.load() != 0)
            {
                return;
            }
            m_pingSentAt = std::chrono::steady_clock::now().time_since_epoch().count();
            SendIfConnected(BuildUpdateRequest(false, 1, 1));
            ++m_statRequests;
        }

        Client::Health Client::GetHealth() const
        {
            using namespace std::chrono;
            Health health;
            steady_clock::rep now = steady_clock::now().time_since_epoch().count();
            health.roundTripMs = m_roundTripMs.load();
            steady_clock::rep sent = m_pingSentAt.load();
            if (sent != 0)
            {
                health.waitingMs = static_cast<int>(duration_cast<milliseconds>(steady_clock::duration(now - sent)).count());
            }
            steady_clock::rep last = m_lastUpdateAt.load();
            if (last != 0)
            {
                health.sinceUpdateMs = static_cast<int>(duration_cast<milliseconds>(steady_clock::duration(now - last)).count());
            }
            return health;
        }

        void Client::RequestFullUpdate()
        {
            // UI thread: must not throw, so use the non-throwing send.
            int width;
            int height;
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                width = m_framebuffer.GetWidth();
                height = m_framebuffer.GetHeight();
            }
            SendIfConnected(BuildUpdateRequest(false, width, height));
            ++m_statRequests;
        }

        void Client::SendUpdateRequest(bool incremental)
        {
            int width;
            int height;
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                width = m_framebuffer.GetWidth();
                height = m_framebuffer.GetHeight();
            }
            Send(BuildUpdateRequest(incremental, width, height));
            ++m_statRequests;
        }

        void Client::SendPixelFormat()
        {
            OutBuffer pixelFormat;
            pixelFormat.U8(ClientMessage::SetPixelFormat).Padding(3);
            PixelFormat::Client().Write(pixelFormat);
            Send(pixelFormat.Data());
        }

        void Client::SendSetupMessages(const ConnectionOptions& options)
        {
            m_resendPixelFormatOnResize = options.resendPixelFormatOnResize;
            SendPixelFormat();

            m_decoders.clear();
            std::vector<int32_t> encodings;
            for (int32_t encoding : options.encodings.empty() ? GetDefaultEncodings() : options.encodings)
            {
                if (std::unique_ptr<Decoder> decoder = CreateDecoder(encoding))
                {
                    m_decoders[encoding] = std::move(decoder);
                    encodings.push_back(encoding);
                }
            }
            if (m_decoders.find(Encoding::Raw) == m_decoders.end())
            {
                // Raw is mandatory for servers to support, and they may fall back to it regardless.
                m_decoders[Encoding::Raw] = CreateDecoder(Encoding::Raw);
                encodings.push_back(Encoding::Raw);
            }
            if (options.exactEncodings)
            {
                SendEncodings(options.encodings);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_baseEncodings = encodings;
                m_wantExtendedClipboard = options.extendedClipboard;
            }
            SendEncodings(ComposeEncodings(options.compressLevel, options.qualityLevel));
        }

        std::vector<int32_t> Client::ComposeEncodings(int compressLevel, int qualityLevel) const
        {
            std::vector<int32_t> encodings;
            bool extendedClipboard;
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                encodings = m_baseEncodings;
                extendedClipboard = m_wantExtendedClipboard;
            }
            if (encodings.empty())
            {
                return encodings;
            }
            bool tight = std::find(encodings.begin(), encodings.end(), Encoding::Tight) != encodings.end();
            bool zrle = std::find(encodings.begin(), encodings.end(), Encoding::Zrle) != encodings.end();
            if (tight || zrle)
            {
                if (compressLevel >= 0 && compressLevel <= 9)
                {
                    encodings.push_back(Encoding::CompressLevel0 + compressLevel);
                }
                if (tight && qualityLevel >= 0 && qualityLevel <= 9)
                {
                    encodings.push_back(Encoding::QualityLevel0 + qualityLevel);
                }
            }
            encodings.push_back(Encoding::ExtendedDesktopSize);
            encodings.push_back(Encoding::DesktopSize);
            encodings.push_back(Encoding::Cursor);
            encodings.push_back(Encoding::LastRect);
            if (extendedClipboard)
            {
                encodings.push_back(Encoding::ExtendedClipboard);
            }
            return encodings;
        }

        Bytes Client::BuildSetEncodings(const std::vector<int32_t>& encodings)
        {
            OutBuffer setEncodings;
            setEncodings.U8(ClientMessage::SetEncodings).Padding(1).U16(static_cast<uint16_t>(encodings.size()));
            for (int32_t encoding : encodings)
            {
                setEncodings.S32(encoding);
            }
            std::string list;
            for (int32_t encoding : encodings)
            {
                list += (list.empty() ? "" : ", ") + std::to_string(encoding);
            }
            LOG_INFO("Rfb", "Asked for encodings [%s]", list.c_str());
            return setEncodings.Data();
        }

        void Client::SendEncodings(const std::vector<int32_t>& encodings)
        {
            Send(BuildSetEncodings(encodings));
        }

        void Client::SetQuality(int compressLevel, int qualityLevel)
        {
            std::vector<int32_t> encodings = ComposeEncodings(compressLevel, qualityLevel);
            if (encodings.empty() || !m_connected)
            {
                return;
            }
            SendIfConnected(BuildSetEncodings(encodings));
        }

        void Client::Run(ConnectionOptions options)
        {
            DisconnectInfo info;
            std::string& error = info.error;
            bool choseTight = false;  // the server and we agreed on TightVNC's own login
            bool handshaking = false; // between connecting and ServerInit
            m_encrypted = false;
            m_extendedClipboard = options.extendedClipboard;
            m_clipboardChannel.Reset();
            m_supportsResize = false;
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_screens.clear();
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_encryptionName.clear();
            }
            try
            {
                Credentials credentials = options.credentials;
                if (options.sshTunnel)
                {
                    OpenTunnel(options, credentials);
                }
                else
                {
                    LOG_INFO("Rfb", "Connecting to %s:%u", options.host.c_str(), options.port);
                    Net::Socket socket;
                    if (!socket.Connect(options.host, options.port, options.connectTimeoutMs, error, &m_stopping))
                    {
                        throw RfbError(error);
                    }
                    std::lock_guard<std::mutex> lock(m_socketMutex);
                    m_socket = std::move(socket);
                    if (m_stopping)
                    {
                        m_socket.Shutdown();
                    }
                }

                ConnectionSource source(*this);
                InStream in(source);
                handshaking = true;

                CredentialsProvider provider = [this, &credentials](CredentialsKind kind, Credentials& out) {
                    // Use what we were given first; ask the user when something is missing. For a Mac offering both
                    // logins, a saved password with no user name means the VNC password.
                    bool complete = !credentials.password.empty() &&
                                    (kind != CredentialsKind::Account || !credentials.username.empty());
                    if (!complete && !m_listener.OnCredentialsRequired(kind, credentials))
                    {
                        return false;
                    }
                    out = credentials;
                    return true;
                };

                TlsStarter startTls;
                bool anonymousTls = options.createTlsStream ? options.createTlsStreamAnonymous
                                                            : Net::IsAnonymousTlsAvailable();
                if (options.allowEncryption && (options.createTlsStream || Net::IsTlsAvailable()))
                {
                    startTls = [this, &options, &in](bool anonymous) { StartTls(anonymous, options, in); };
                }
                // A server that accepts the connection but then says nothing mustn't leave us waiting for ever.
                SetReceiveTimeout(options.connectTimeoutMs);
                HandshakeResult handshake =
                    PerformHandshake(in, *this, options.shared, provider, startTls, options.allowTight, &choseTight,
                                     anonymousTls);
                m_fileTransfer = handshake.tightFileTransfer;
                SetReceiveTimeout(0);
                CheckDesktopSize(handshake.serverInit.width, handshake.serverInit.height);
                m_appleServer = handshake.serverMajor == 3 && handshake.serverMinor == 889;
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    m_framebuffer.Resize(handshake.serverInit.width, handshake.serverInit.height);
                }
                SendSetupMessages(options);
                m_detailedUpdates = kDetailedUpdates;
                m_sendQueue.Start();
                m_connected = true;
                info.wasConnected = true;
                handshaking = false;
                m_listener.OnConnected(handshake.serverInit);
                SendUpdateRequest(false);
                MessageLoop(in);
            }
            catch (const AuthenticationCancelled&)
            {
                error.clear();
            }
            catch (const AuthenticationFailed& exception)
            {
                error = exception.what();
                info.authenticationFailed = true;
            }
            catch (const EncryptionUnavailable& exception)
            {
                error = exception.what();
                info.encryptionUnusable = true;
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                // Something about TightVNC's own login went wrong (not the password): the next attempt goes without.
                info.tightFailed = choseTight && handshaking;
            }

            m_connected = false;
            m_fileTransfer = false;
            {
                // A send stuck on a slow link mustn't hold up the end of the connection: shutting down gets it out.
                std::lock_guard<std::mutex> socketLock(m_socketMutex);
                m_socket.Shutdown();
                if (m_tunnel)
                {
                    m_tunnel->Shutdown();
                }
            }
            m_sendQueue.Stop();
            {
                std::lock_guard<std::mutex> sendLock(m_sendMutex);
                std::lock_guard<std::mutex> socketLock(m_socketMutex);
                if (m_tls)
                {
                    m_tls->Shutdown();
                    m_tls.reset();
                }
                if (m_tunnel)
                {
                    m_tunnel->Shutdown(); // a writer still holding a copy gets a failure, not a dangling pointer
                    m_tunnel.reset();
                }
                m_socket.Close();
            }
            if (m_stopping)
            {
                error.clear();
                info.authenticationFailed = false;
            }
            m_pingSentAt = 0; // nothing is waiting for an answer any more
            if (error.empty())
            {
                LOG_INFO("Rfb", "Disconnected");
            }
            else
            {
                LOG_WARNING("Rfb", "Disconnected: %s", error.c_str());
            }
            m_listener.OnDisconnected(info);
        }

        void Client::OpenTunnel(const ConnectionOptions& options, Credentials& credentials)
        {
            LOG_INFO("Rfb", "Connecting to %s through SSH (port %u) for VNC port %u", options.host.c_str(),
                     options.sshPort, options.port);
            std::shared_ptr<Net::SshTunnel> tunnel = std::make_shared<Net::SshTunnel>();
            {
                std::lock_guard<std::mutex> lock(m_socketMutex);
                m_tunnel = tunnel;
                if (m_stopping)
                {
                    tunnel->Shutdown();
                }
            }
            Net::SshTunnelOptions tunnelOptions;
            tunnelOptions.host = options.host;
            tunnelOptions.port = options.sshPort;
            tunnelOptions.trustedFingerprint = options.trustedSshHostKey;
            tunnelOptions.timeoutMs = options.connectTimeoutMs;
            tunnelOptions.trustDecision = [this](const Net::SshHostKeyQuestion& question) {
                LOG_INFO("Rfb", "The SSH host key for %s (%s %s) isn't the trusted one; asking", question.host.c_str(),
                         question.algorithm.c_str(), question.fingerprint.c_str());
                return m_listener.OnUntrustedSshHostKey(question);
            };
            std::string error;
            bool refused = false;
            if (!tunnel->Connect(tunnelOptions, error, refused, &m_stopping))
            {
                if (refused)
                {
                    throw AuthenticationCancelled(error);
                }
                throw RfbError(error);
            }
            // The SSH login: the connection's credentials, or ask. A rejection ends the attempt like a rejected VNC
            // password does, so the viewer offers to try again with fresh credentials.
            bool complete = !credentials.username.empty() && !credentials.password.empty();
            if (!complete && !m_listener.OnCredentialsRequired(CredentialsKind::SshAccount, credentials))
            {
                throw AuthenticationCancelled("The SSH login was cancelled");
            }
            if (!tunnel->Login(credentials.username, credentials.password, error))
            {
                throw AuthenticationFailed("SSH login to " + options.host + " failed: " + error);
            }
            if (!tunnel->Open("127.0.0.1", options.port, error))
            {
                throw RfbError(error + " (is the VNC server running on that computer?)");
            }
            m_encrypted = true;
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_encryptionName = "SSH";
            }
            LOG_INFO("Rfb", "Connected through SSH to %s (host key %s)", options.host.c_str(),
                     tunnel->GetFingerprint().c_str());
        }

        void Client::MessageLoop(InStream& in)
        {
            for (;;)
            {
                uint8_t type = in.U8();
                switch (type)
                {
                case ServerMessage::FramebufferUpdate:
                    HandleFramebufferUpdate(in);
                    SendUpdateRequest(true);
                    break;
                case ServerMessage::SetColourMapEntries:
                {
                    // We always ask for true colour, so a colour map is only ever informational.
                    in.Skip(3);
                    uint16_t count = in.U16();
                    in.Skip(static_cast<size_t>(count) * 6);
                    break;
                }
                case ServerMessage::Bell:
                    m_listener.OnBell();
                    break;
                case ServerMessage::ServerCutText:
                    HandleServerCutText(in);
                    break;
                case 0xFC: // TightVNC extension messages: a 4-byte id starting 0xFC
                {
                    uint32_t id = 0xFC000000u | static_cast<uint32_t>(in.U8()) << 16;
                    id |= in.U16();
                    if (!m_fileTransfer || !FileTransfer::IsReply(id))
                    {
                        throw RfbError("The server sent an unknown TightVNC message");
                    }
                    FileTransfer::Reply reply = FileTransfer::ReadReply(id, in);
                    std::lock_guard<std::mutex> lock(m_fileTransferMutex);
                    if (m_fileTransferHandler)
                    {
                        m_fileTransferHandler(reply);
                    }
                    break;
                }
                default:
                    throw RfbError("The server sent an unknown message type " + std::to_string(type));
                }
            }
        }

        void Client::HandleFramebufferUpdate(InStream& in)
        {
            in.Skip(1);
            uint16_t count = in.U16();
            std::vector<Rect> dirty;
            std::vector<std::string> described; // "WxH+X+Y enc N" per rectangle, for the log
            bool resized = false;

            // A count of 0xFFFF with LastRect means "until a LastRect rectangle".
            for (uint32_t i = 0; i < count; ++i)
            {
                Rect rect;
                rect.x = in.U16();
                rect.y = in.U16();
                rect.width = in.U16();
                rect.height = in.U16();
                int32_t encoding = in.S32();
                if (m_detailedUpdates > 0)
                {
                    described.push_back(std::to_string(rect.width) + "x" + std::to_string(rect.height) + "+" +
                                        std::to_string(rect.x) + "+" + std::to_string(rect.y) + " enc " +
                                        std::to_string(encoding));
                }

                if (encoding == Encoding::LastRect)
                {
                    break;
                }
                if (encoding == Encoding::DesktopSize)
                {
                    ResizeFramebuffer(rect.width, rect.height);
                    resized = true;
                    continue;
                }
                if (encoding == Encoding::ExtendedDesktopSize)
                {
                    HandleExtendedDesktopSize(in, rect);
                    resized = true;
                    continue;
                }
                if (encoding == Encoding::Cursor)
                {
                    HandleCursor(in, rect);
                    continue;
                }

                auto found = m_decoders.find(encoding);
                if (found == m_decoders.end())
                {
                    throw RfbError("The server used an encoding we didn't ask for (" + std::to_string(encoding) + ")");
                }
                bool inside;
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    inside = m_framebuffer.Contains(rect);
                }
                if (!inside)
                {
                    throw RfbError("The server sent a rectangle outside the desktop");
                }
                if (IsAppleMislabelledRect(rect))
                {
                    dirty.push_back(DecodeAppleMislabelledRect(*found->second, in, rect));
                    continue;
                }
                if (m_appleServer && encoding != Encoding::CopyRect && IsWholeDesktop(rect))
                {
                    if (!DecodeAppleWholeDesktop(*found->second, in, rect))
                    {
                        continue; // garbled: leave the framebuffer as it was
                    }
                    dirty.push_back(rect);
                    continue;
                }
                found->second->Decode(in, rect, m_framebuffer);
                dirty.push_back(rect);
            }
            ++m_statUpdates;
            m_statRectangles += static_cast<int>(dirty.size());
            {
                // The first update after a ping is taken as its answer. An incremental update already on its way
                // can arrive first and make the figure a little low; for a health readout that's close enough.
                using namespace std::chrono;
                steady_clock::rep now = steady_clock::now().time_since_epoch().count();
                m_lastUpdateAt = now;
                steady_clock::rep sent = m_pingSentAt.exchange(0);
                if (sent != 0)
                {
                    m_roundTripMs = static_cast<int>(duration_cast<milliseconds>(steady_clock::duration(now - sent)).count());
                }
            }

            if (m_detailedUpdates > 0)
            {
                // The first few updates after connecting or a resize, rectangle by rectangle.
                --m_detailedUpdates;
                std::string rects;
                for (size_t i = 0; i < described.size() && i < 12; ++i)
                {
                    rects += (rects.empty() ? "" : ", ") + described[i];
                }
                if (described.size() > 12)
                {
                    rects += ", ... (" + std::to_string(described.size()) + " in all)";
                }
                bool black;
                int width;
                int height;
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    black = m_framebuffer.IsBlack();
                    width = m_framebuffer.GetWidth();
                    height = m_framebuffer.GetHeight();
                }
                LOG_INFO("Rfb", "Update on a %dx%d desktop: %s%s", width, height, rects.empty() ? "no rectangles" : rects.c_str(),
                         black ? "; desktop is still all black" : "");
            }

            if (!dirty.empty())
            {
                m_listener.OnFramebufferUpdated(dirty);
            }
            if (resized)
            {
                // macOS may forget our pixel format when it switches display modes; say it again (harmless
                // elsewhere) before asking for the whole desktop.
                if (m_resendPixelFormatOnResize)
                {
                    SendPixelFormat();
                }
                SendUpdateRequest(false);
            }
        }

        bool Client::IsAppleMislabelledRect(const Rect& rect)
        {
            // macOS (RFB 3.889) on a Retina display, while switching between its scaled and native modes, sends a
            // rectangle labelled with the scaled size plus a 2 pixel border (e.g. 1922x1082 on a 3840x2160 desktop),
            // but fills it with consecutive rows of the full-resolution screen. Drawn as labelled it comes out
            // sheared, with its halves swapped.
            if (!m_appleServer || rect.x != 0 || rect.y != 0)
            {
                return false;
            }
            std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
            int width = m_framebuffer.GetWidth();
            int height = m_framebuffer.GetHeight();
            return width >= 640 && height >= 480 && width % 2 == 0 && height % 2 == 0 &&
                   rect.width == width / 2 + 2 && rect.height == height / 2 + 2;
        }

        Rect Client::DecodeAppleMislabelledRect(Decoder& decoder, InStream& in, const Rect& rect)
        {
            // Decode as labelled into a scratch image, then lay the pixel stream out at the desktop's real width.
            Framebuffer scratch;
            scratch.Resize(rect.width, rect.height);
            decoder.Decode(in, {0, 0, rect.width, rect.height}, scratch);

            std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
            const int width = m_framebuffer.GetWidth();
            const size_t pixels = static_cast<size_t>(rect.width) * rect.height; // scratch rows are contiguous
            const int rows = std::min(static_cast<int>(pixels / width), m_framebuffer.GetHeight());
            for (int y = 0; y < rows; ++y)
            {
                std::memcpy(m_framebuffer.Row(y), scratch.Row(0) + static_cast<size_t>(y) * width,
                            static_cast<size_t>(width) * sizeof(uint32_t));
            }
            LOG_INFO("Rfb", "Repaired a mislabelled %dx%d rectangle from macOS into the top %d rows", rect.width,
                     rect.height, rows);
            return {0, 0, width, rows};
        }

        bool Client::IsWholeDesktop(const Rect& rect)
        {
            std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
            return rect.x == 0 && rect.y == 0 && rect.width == m_framebuffer.GetWidth() &&
                   rect.height == m_framebuffer.GetHeight();
        }

        bool Client::DecodeAppleWholeDesktop(Decoder& decoder, InStream& in, const Rect& rect)
        {
            // macOS sometimes sends a picture of the whole (scaled) desktop that really holds consecutive rows of
            // its full-resolution screen. Decode into a scratch image and only keep it if it isn't garbled.
            Framebuffer scratch;
            scratch.Resize(rect.width, rect.height);
            decoder.Decode(in, {0, 0, rect.width, rect.height}, scratch);
            if (LooksRowInterleaved(scratch))
            {
                LOG_INFO("Rfb", "Discarded a garbled %dx%d picture from macOS", rect.width, rect.height);
                return false;
            }
            std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
            m_framebuffer.PutPixels(rect, scratch.Row(0), static_cast<size_t>(rect.width));
            return true;
        }

        void Client::ResizeFramebuffer(int width, int height)
        {
            CheckDesktopSize(width, height);
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                if (width == m_framebuffer.GetWidth() && height == m_framebuffer.GetHeight())
                {
                    return;
                }
                m_framebuffer.Resize(width, height);
            }
            LOG_INFO("Rfb", "Desktop resized to %dx%d", width, height);
            m_detailedUpdates = kDetailedUpdates;
            m_listener.OnDesktopResized(width, height);
        }

        void Client::HandleExtendedDesktopSize(InStream& in, const Rect& rect)
        {
            // x = reason (0 server, 1 our request, 2 another client), y = status, w/h = new size; then screens.
            uint8_t count = in.U8();
            in.Skip(3);
            std::vector<Screen> screens;
            for (uint8_t i = 0; i < count; ++i)
            {
                Screen screen;
                screen.id = in.U32();
                screen.x = in.U16();
                screen.y = in.U16();
                screen.width = in.U16();
                screen.height = in.U16();
                screen.flags = in.U32();
                screens.push_back(screen);
            }
            m_supportsResize = true;
            if (rect.x == 1 && rect.y != 0)
            {
                LOG_WARNING("Rfb", "The server refused to resize the desktop (status %d)", rect.y);
                m_listener.OnDesktopResizeRefused(rect.y);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_screens = screens;
            }
            ResizeFramebuffer(rect.width, rect.height);
            LOG_INFO("Rfb", "The server has %zu display%s", screens.size(), screens.size() == 1 ? "" : "s");
            m_listener.OnScreenLayout(screens);
        }

        std::vector<Screen> Client::GetScreens() const
        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
            return m_screens;
        }

        Bytes Client::BuildSetDesktopSize(int width, int height, const std::vector<Screen>& screens)
        {
            // One display covering the whole desktop, keeping the first display's id and flags so the server sees
            // a change of size rather than a new display.
            Screen screen;
            screen.id = 1;
            if (!screens.empty())
            {
                screen.id = screens[0].id;
                screen.flags = screens[0].flags;
            }
            OutBuffer message;
            message.U8(ClientMessage::SetDesktopSize).Padding(1);
            message.U16(static_cast<uint16_t>(width)).U16(static_cast<uint16_t>(height));
            message.U8(1).Padding(1);
            message.U32(screen.id).U16(0).U16(0);
            message.U16(static_cast<uint16_t>(width)).U16(static_cast<uint16_t>(height)).U32(screen.flags);
            return message.Data();
        }

        void Client::RequestDesktopSize(int width, int height)
        {
            if (!m_supportsResize || width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF)
            {
                return;
            }
            SendIfConnected(BuildSetDesktopSize(width, height, GetScreens()));
            LOG_INFO("Rfb", "Asked the server to change its screen to %dx%d", width, height);
        }

        void Client::HandleCursor(InStream& in, const Rect& rect)
        {
            // Pixels in our pixel format, then a 1bpp mask with rows padded to a byte. x/y are the hotspot.
            CursorShape cursor;
            cursor.width = rect.width;
            cursor.height = rect.height;
            cursor.hotspotX = rect.x;
            cursor.hotspotY = rect.y;
            const size_t pixelCount = static_cast<size_t>(rect.width) * rect.height;
            if (pixelCount > 1024 * 1024)
            {
                throw RfbError("The server sent an oversized cursor");
            }
            cursor.argb.resize(pixelCount);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                cursor.argb[i] = in.Pixel32() & 0x00FFFFFFu;
            }
            const size_t rowBytes = (static_cast<size_t>(rect.width) + 7) / 8;
            Bytes mask = in.ReadBytes(rowBytes * rect.height);
            for (int y = 0; y < rect.height; ++y)
            {
                for (int x = 0; x < rect.width; ++x)
                {
                    if (mask[y * rowBytes + x / 8] & (0x80 >> (x % 8)))
                    {
                        cursor.argb[static_cast<size_t>(y) * rect.width + x] |= 0xFF000000u;
                    }
                }
            }
            m_listener.OnCursorShape(cursor);
        }

        void Client::HandleServerCutText(InStream& in)
        {
            in.Skip(3);
            int32_t length = in.S32();
            if (length < 0)
            {
                // An Extended Clipboard message: U32 flags, then the body.
                size_t size = static_cast<size_t>(-static_cast<int64_t>(length));
                if (size > kMaxCutTextLength)
                {
                    throw RfbError("The server sent an oversized clipboard update");
                }
                if (!m_extendedClipboard)
                {
                    LOG_INFO("Rfb", "The server sent an extended clipboard message we didn't ask for; skipping it");
                    in.Skip(size);
                    return;
                }
                m_clipboardChannel.HandleServerMessage(in.ReadBytes(size));
                return;
            }
            if (static_cast<uint32_t>(length) > kMaxCutTextLength)
            {
                throw RfbError("The server sent an oversized clipboard update");
            }
            LOG_INFO("Rfb", "The server sent its clipboard (%d characters)", length);
            m_listener.OnServerCutText(in.ReadString(static_cast<size_t>(length)));
        }
    } // namespace Rfb
} // namespace Marconi
