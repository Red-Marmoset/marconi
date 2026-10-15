// Copyright (c) 2026 Red Marmoset Studios Ltd.
// VNC client connection: owns the socket and a worker thread that runs the RFB protocol

#include <RmVncCore/Rfb/Client.h>

#include <Log/Log.h>

#include <algorithm>

namespace RmVnc
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kMaxCutTextLength = 16 * 1024 * 1024;
            // Refuse absurd desktops from broken or hostile servers (16k x 16k is already 1 GiB of pixels).
            constexpr int kMaxDesktopSide = 16384;

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

        Client::Client(ClientListener& listener) : m_listener(listener)
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
            m_thread = std::thread(&Client::Run, this, options);
        }

        void Client::Disconnect()
        {
            m_stopping = true;
            {
                std::lock_guard<std::mutex> lock(m_socketMutex);
                m_socket.Shutdown();
            }
            if (m_thread.joinable())
            {
                if (m_thread.get_id() == std::this_thread::get_id())
                {
                    // Called from a listener callback: the thread finishes by itself once the socket is shut down.
                    m_thread.detach();
                    return;
                }
                m_thread.join();
            }
        }

        void Client::Send(const Bytes& data)
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            if (!m_socket.SendAll(data.data(), data.size()))
            {
                throw RfbError("Connection lost while sending");
            }
        }

        void Client::SendIfConnected(const Bytes& data)
        {
            if (!m_connected)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_sendMutex);
            // A failure here is reported by the connection thread when its next read fails.
            m_socket.SendAll(data.data(), data.size());
        }

        void Client::SendPointerEvent(int x, int y, uint8_t buttonMask)
        {
            OutBuffer message;
            message.U8(ClientMessage::PointerEvent)
                .U8(buttonMask)
                .U16(static_cast<uint16_t>(std::max(x, 0)))
                .U16(static_cast<uint16_t>(std::max(y, 0)));
            SendIfConnected(message.Data());
        }

        void Client::SendKeyEvent(uint32_t keysym, bool down)
        {
            OutBuffer message;
            message.U8(ClientMessage::KeyEvent).U8(down ? 1 : 0).Padding(2).U32(keysym);
            SendIfConnected(message.Data());
        }

        void Client::SendClientCutText(const std::string& latin1Text)
        {
            OutBuffer message;
            message.U8(ClientMessage::ClientCutText).Padding(3).U32(static_cast<uint32_t>(latin1Text.size())).Append(latin1Text);
            SendIfConnected(message.Data());
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
        }

        void Client::SendSetupMessages(const ConnectionOptions& options)
        {
            OutBuffer pixelFormat;
            pixelFormat.U8(ClientMessage::SetPixelFormat).Padding(3);
            PixelFormat::Client().Write(pixelFormat);
            Send(pixelFormat.Data());

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
            if (m_decoders.count(Encoding::Tight) != 0 || m_decoders.count(Encoding::Zrle) != 0)
            {
                if (options.compressLevel >= 0 && options.compressLevel <= 9)
                {
                    encodings.push_back(Encoding::CompressLevel0 + options.compressLevel);
                }
                if (m_decoders.count(Encoding::Tight) != 0 && options.qualityLevel >= 0 && options.qualityLevel <= 9)
                {
                    encodings.push_back(Encoding::QualityLevel0 + options.qualityLevel);
                }
            }
            encodings.push_back(Encoding::ExtendedDesktopSize);
            encodings.push_back(Encoding::DesktopSize);
            encodings.push_back(Encoding::Cursor);
            encodings.push_back(Encoding::LastRect);

            OutBuffer setEncodings;
            setEncodings.U8(ClientMessage::SetEncodings).Padding(1).U16(static_cast<uint16_t>(encodings.size()));
            for (int32_t encoding : encodings)
            {
                setEncodings.S32(encoding);
            }
            Send(setEncodings.Data());
        }

        void Client::Run(ConnectionOptions options)
        {
            DisconnectInfo info;
            std::string& error = info.error;
            try
            {
                LOG_INFO("Rfb", "Connecting to %s:%u", options.host.c_str(), options.port);
                Net::Socket socket;
                if (!socket.Connect(options.host, options.port, options.connectTimeoutMs, error, &m_stopping))
                {
                    throw RfbError(error);
                }
                {
                    std::lock_guard<std::mutex> lock(m_socketMutex);
                    m_socket = std::move(socket);
                    if (m_stopping)
                    {
                        m_socket.Shutdown();
                    }
                }

                SocketSource source(m_socket);
                InStream in(source);

                Credentials credentials = options.credentials;
                CredentialsProvider provider = [this, &credentials](bool needUsername, Credentials& out) {
                    // Use what we were given first; ask the user when something is missing.
                    bool complete = !credentials.password.empty() && (!needUsername || !credentials.username.empty());
                    if (!complete && !m_listener.OnCredentialsRequired(needUsername, credentials))
                    {
                        return false;
                    }
                    out = credentials;
                    return true;
                };

                // A user name means "log in with my account" on a Mac that also offers a VNC password.
                HandshakeResult handshake =
                    PerformHandshake(in, *this, options.shared, provider, !options.credentials.username.empty());
                CheckDesktopSize(handshake.serverInit.width, handshake.serverInit.height);
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    m_framebuffer.Resize(handshake.serverInit.width, handshake.serverInit.height);
                }
                SendSetupMessages(options);
                m_connected = true;
                info.wasConnected = true;
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
            catch (const std::exception& exception)
            {
                error = exception.what();
            }

            m_connected = false;
            {
                std::lock_guard<std::mutex> sendLock(m_sendMutex);
                std::lock_guard<std::mutex> socketLock(m_socketMutex);
                m_socket.Close();
            }
            if (m_stopping)
            {
                error.clear();
                info.authenticationFailed = false;
            }
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
                found->second->Decode(in, rect, m_framebuffer);
                dirty.push_back(rect);
            }

            if (!dirty.empty())
            {
                m_listener.OnFramebufferUpdated(dirty);
            }
            if (resized)
            {
                SendUpdateRequest(false);
            }
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
            m_listener.OnDesktopResized(width, height);
        }

        void Client::HandleExtendedDesktopSize(InStream& in, const Rect& rect)
        {
            // x = reason (0 server, 1 our request, 2 another client), y = status, w/h = new size; then screens.
            uint8_t screens = in.U8();
            in.Skip(3);
            in.Skip(static_cast<size_t>(screens) * 16);
            if (rect.x == 1 && rect.y != 0)
            {
                LOG_WARNING("Rfb", "The server refused to resize the desktop (status %d)", rect.y);
                return;
            }
            ResizeFramebuffer(rect.width, rect.height);
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
                // Extended clipboard pseudo-message; we don't advertise it, but skip it safely.
                in.Skip(static_cast<size_t>(-static_cast<int64_t>(length)));
                return;
            }
            if (static_cast<uint32_t>(length) > kMaxCutTextLength)
            {
                throw RfbError("The server sent an oversized clipboard update");
            }
            m_listener.OnServerCutText(in.ReadString(static_cast<size_t>(length)));
        }
    } // namespace Rfb
} // namespace RmVnc
