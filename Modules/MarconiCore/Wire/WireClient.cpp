// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol client

#include <MarconiCore/Wire/WireClient.h>

#include <Log/Log.h>

#include <zlib.h>

#include <algorithm>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
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
            {
                std::lock_guard<std::mutex> lock(m_writeMutex);
                m_transport = std::make_unique<SocketTransport>(m_socket);
            }
            MessageReader reader(*m_transport);
            try
            {
                if (!Handshake(reader, options, info))
                {
                    std::lock_guard<std::mutex> lock(m_writeMutex);
                    m_transport.reset();
                    m_connected = false;
                    m_listener.OnDisconnected(info);
                    return;
                }
                while (!m_stopping)
                {
                    Bytes bytes = reader.Next();
                    Reader body(bytes);
                    Type type = ReadType(body);
                    switch (type)
                    {
                    case Type::FrameBegin:
                        FrameBegin::Decode(body);
                        m_frameRects.clear();
                        break;
                    case Type::Still:
                        HandleStill(Still::Decode(body));
                        break;
                    case Type::Move:
                        HandleMove(Move::Decode(body));
                        break;
                    case Type::FrameEnd:
                        FrameEnd::Decode(body);
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
                        Pong::Decode(body);
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
            }
            m_connected = false;
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
                uLongf out = static_cast<uLongf>(byteCount);
                std::vector<uint8_t> raw(byteCount);
                int result = uncompress(raw.data(), &out, still.data.data(), static_cast<uLong>(still.data.size()));
                if (result != Z_OK || out != byteCount)
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
            return m_encrypted.load() ? "TLS" : "";
        }

        std::chrono::steady_clock::duration Client::GetTimeSinceInput() const
        {
            std::chrono::steady_clock::time_point last{
                std::chrono::steady_clock::duration{m_lastInputTime.load()}};
            return std::chrono::steady_clock::now() - last;
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
