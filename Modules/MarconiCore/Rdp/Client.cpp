// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The RDP client: the connection sequence, then the live loop that draws the server's bitmap updates and sends input.

#include <MarconiCore/Rdp/Client.h>

#include <MarconiCore/Rdp/Bitmap.h>
#include <MarconiCore/Rdp/Connect.h>
#include <MarconiCore/Rdp/Input.h>
#include <MarconiCore/Rdp/Output.h>
#include <MarconiCore/Rdp/Pdu.h>

#include <Log/Log.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // VNC-style pointer button mask bits (what the viewer sends), to translate into RDP's per-event flags.
            constexpr uint8_t kMaskLeft = 0x01;
            constexpr uint8_t kMaskMiddle = 0x02;
            constexpr uint8_t kMaskRight = 0x04;
            constexpr uint8_t kMaskWheelUp = 0x08;
            constexpr uint8_t kMaskWheelDown = 0x10;

            constexpr uint16_t kUpdateTypeBitmap = 0x0001; // TS_UPDATE_BITMAP's updateType

            // One wheel notch, as RDP's signed 8-bit rotation in the low byte of the pointer flags.
            constexpr uint16_t kWheelNotch = 0x0078;              // +120, scroll up
            constexpr uint16_t kWheelNotchNegative = 0x0088;      // -120 in two's complement, scroll down
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
            {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                if (m_connection)
                {
                    m_connection->Shutdown();
                }
            }
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

        void Client::MarkInput()
        {
            m_lastInputTime = std::chrono::steady_clock::now().time_since_epoch().count();
        }

        void Client::Run(ClientOptions options)
        {
            DisconnectInfo info;

            // An empty password means we have no credentials yet: ask the viewer for an account (user and password).
            if (options.password.empty())
            {
                Rfb::Credentials credentials;
                credentials.username = options.user;
                if (!m_listener.OnCredentialsRequired(Rfb::CredentialsKind::Account, credentials))
                {
                    m_listener.OnDisconnected(info); // cancelled: a clean, empty-error disconnect
                    return;
                }
                options.user = credentials.username;
                options.password = credentials.password;
            }

            auto connection = std::make_unique<Connection>();

            std::vector<uint8_t> pinned = options.trustedCertificateSha256;
            auto trust = [this, pinned](const Net::TlsCertificate& certificate) {
                if (!pinned.empty() && certificate.sha256 == pinned)
                {
                    return true;
                }
                return m_listener.OnUntrustedCertificate(certificate);
            };

            CredSspCredentials credentials;
            credentials.domain = options.domain;
            credentials.user = options.user;
            credentials.password = options.password;

            std::string error;
            if (!connection->Open(options.host, options.port, credentials, trust, options.connectTimeoutMs, error,
                                  &m_stopping))
            {
                const ConnectOutcome& outcome = connection->Outcome();
                info.error = m_stopping ? "" : error;
                info.authenticationFailed = outcome.usedNla && !outcome.authenticated;
                m_listener.OnDisconnected(info);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_nameMutex);
                m_encryptionName = connection->Outcome().tlsName;
            }
            m_encrypted = true;

            LogonInfo logon;
            logon.domain = options.domain;
            logon.user = options.user;
            logon.password = options.password;
            if (!connection->Activate(options.width, options.height, options.clientName, logon, error))
            {
                info.error = m_stopping ? "" : error;
                info.encryptionUnusable = false;
                m_listener.OnDisconnected(info);
                return;
            }

            // The desktop is up: size the framebuffer to the size the server gave us and tell the viewer.
            m_framebuffer.Resize(options.width, options.height);
            Connection* live = connection.get();
            {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                m_connection = std::move(connection);
            }
            m_connected = true;

            Rfb::ServerInit serverInit;
            serverInit.width = options.width;
            serverInit.height = options.height;
            serverInit.name = options.host;
            m_listener.OnConnected(serverInit);
            LOG_INFO("Rdp", "RDP desktop up: %s, %ux%u, %s", options.host.c_str(), options.width, options.height,
                     live->Outcome().tlsName.c_str());

            live->UseBlockingReads();
            while (!m_stopping)
            {
                Bytes payload;
                bool fastPath = false;
                if (!live->ReadOutput(payload, fastPath, error))
                {
                    if (!m_stopping)
                    {
                        info.error = error;
                        info.wasConnected = true;
                    }
                    break;
                }

                m_frameRects.clear();
                std::string parseError;
                if (fastPath)
                {
                    std::vector<FastPathUpdate> updates;
                    if (ParseFastPathUpdates(payload, updates, parseError))
                    {
                        for (const FastPathUpdate& update : updates)
                        {
                            if (update.type == FastPathUpdateType::Bitmap)
                            {
                                for (const BitmapRect& rect : update.rects)
                                {
                                    ApplyRect(rect);
                                }
                            }
                        }
                    }
                }
                else if (!payload.empty())
                {
                    ShareDataType type2 = ShareDataType::Update;
                    Bytes body;
                    if (ReadShareData(payload, type2, body) && type2 == ShareDataType::Update && body.size() >= 2)
                    {
                        uint16_t updateType = static_cast<uint16_t>(body[0]) | (static_cast<uint16_t>(body[1]) << 8);
                        if (updateType == kUpdateTypeBitmap)
                        {
                            std::vector<BitmapRect> rects;
                            Bytes afterType(body.begin() + 2, body.end());
                            if (ParseBitmapUpdate(afterType, rects, parseError))
                            {
                                for (const BitmapRect& rect : rects)
                                {
                                    ApplyRect(rect);
                                }
                            }
                        }
                    }
                }

                if (!m_frameRects.empty())
                {
                    ++m_statUpdates;
                    m_listener.OnFramebufferUpdated(m_frameRects);
                }
            }

            m_connected = false;
            m_encrypted = false;
            {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                m_connection.reset();
            }
            m_listener.OnDisconnected(info);
        }

        void Client::ApplyRect(const BitmapRect& rect)
        {
            if (rect.width == 0 || rect.height == 0 || rect.data.empty())
            {
                return;
            }
            std::vector<uint32_t> pixels;
            std::string error;
            bool decoded =
                rect.compressed
                    ? DecodeInterleavedRle(rect.data.data(), rect.data.size(), rect.width, rect.height,
                                           rect.bitsPerPixel, nullptr, pixels, error)
                    : DecodeUncompressedBitmap(rect.data.data(), rect.data.size(), rect.width, rect.height,
                                               rect.bitsPerPixel, nullptr, pixels, error);
            if (!decoded)
            {
                LOG_WARNING("Rdp", "Dropped a %ubpp bitmap rectangle: %s", rect.bitsPerPixel, error.c_str());
                return;
            }
            const Rfb::Rect target{rect.x, rect.y, rect.width, rect.height};
            {
                std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                if (!m_framebuffer.Contains(target))
                {
                    return; // outside the desktop (a resize we haven't applied): drop it
                }
                m_framebuffer.PutPixels(target, pixels.data(), static_cast<size_t>(rect.width));
            }
            ++m_statRectangles;
            m_frameRects.push_back(target);
        }

        void Client::SendInput(const std::vector<Bytes>& events)
        {
            if (events.empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_connectionMutex);
            if (!m_connection || !m_connected.load())
            {
                return;
            }
            std::string error;
            Bytes pdu = BuildInputPdu(m_connection->ShareId(), m_connection->UserChannel(), events);
            if (!m_connection->SendOnIoChannel(pdu, error))
            {
                // A send failure means the connection is going away; the worker's read will see it and disconnect.
            }
        }

        void Client::SendPointerEvent(int x, int y, uint8_t buttonMask)
        {
            MarkInput();
            const uint16_t px = static_cast<uint16_t>(x < 0 ? 0 : x);
            const uint16_t py = static_cast<uint16_t>(y < 0 ? 0 : y);
            std::vector<Bytes> events = PointerEvents(buttonMask, m_buttonState, px, py);
            m_buttonState = buttonMask;
            ++m_statPointerEvents;
            SendInput(events);
        }

        void Client::SendKeyEvent(uint32_t keysym, bool down)
        {
            MarkInput();
            ++m_statKeyEvents;
            Scancode scancode = KeysymToScancode(keysym);
            if (scancode.known)
            {
                SendInput({ScancodeEvent(scancode, down)});
                return;
            }
            // No scancode for this key: send it as a Unicode character where we can work one out.
            uint32_t codePoint = 0;
            if (keysym < 0x100)
            {
                codePoint = keysym; // Latin-1 keysyms are their code points
            }
            else if (keysym >= 0x01000000 && keysym <= 0x0110FFFF)
            {
                codePoint = keysym - 0x01000000; // the X11 Unicode keysym range
            }
            if (codePoint == 0 || codePoint > 0xFFFF)
            {
                return; // nothing we can send as a single UTF-16 code unit
            }
            SendInput({UnicodeEvent(static_cast<uint16_t>(codePoint), down)});
        }

        void Client::SendClientCutText(const std::string& /*latin1Text*/)
        {
            // Clipboard over RDP (the cliprdr virtual channel) is a later milestone; nothing to send yet.
        }

        bool Client::OfferClipboard(const Rfb::ClipboardContent& /*content*/)
        {
            return false; // not active yet
        }

        void Client::RequestFullUpdate()
        {
            // RDP pushes updates as the desktop changes; a Refresh Rect PDU could force a repaint, added later.
        }

        void Client::RequestDesktopSize(int /*width*/, int /*height*/)
        {
            // Server-side resize (the Display Control channel) is a later milestone.
        }

        void Client::SetQuality(int /*compressLevel*/, int /*qualityLevel*/)
        {
            // RDP quality is fixed by the capabilities we negotiate; nothing to change live yet.
        }

        std::string Client::GetEncryptionName() const
        {
            std::lock_guard<std::mutex> lock(m_nameMutex);
            return m_encryptionName;
        }

        std::chrono::steady_clock::duration Client::GetTimeSinceInput() const
        {
            std::chrono::steady_clock::time_point last{std::chrono::steady_clock::duration{m_lastInputTime.load()}};
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
    } // namespace Rdp
} // namespace Marconi
