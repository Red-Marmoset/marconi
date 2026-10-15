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

                HandshakeResult handshake = PerformHandshake(in, *this, options.shared, provider);
                CheckDesktopSize(handshake.serverInit.width, handshake.serverInit.height);
                m_appleServer = handshake.serverMajor == 3 && handshake.serverMinor == 889;
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    m_framebuffer.Resize(handshake.serverInit.width, handshake.serverInit.height);
                }
                SendSetupMessages(options);
                m_detailedUpdates = kDetailedUpdates;
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
                SendPixelFormat();
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
                LOG_INFO("Rfb", "The server sent an extended clipboard message, which we don't support");
                in.Skip(static_cast<size_t>(-static_cast<int64_t>(length)));
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
