// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Remote Desktop (RDP) client: connects, then turns the server's updates into framebuffer pixels and the viewer's
// input into RDP input events.

#include <MarconiCore/Rdp/RdpClient.h>

#include <MarconiCore/Rdp/Bitmap.h>
#include <MarconiCore/Rdp/Connect.h>
#include <MarconiCore/Rdp/Input.h>
#include <MarconiCore/Rdp/Mcs.h>
#include <MarconiCore/Rdp/Pdu.h>
#include <MarconiCore/Rdp/Pointer.h>
#include <MarconiCore/Rdp/Sequence.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstdio>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // Share Data PDU types beyond the ones Pdu.h names: the server's reasons and session notices.
            constexpr uint8_t kPduTypeSaveSessionInfo = 38;
            constexpr uint8_t kPduTypeSetErrorInfo = 47;

            // The NTSTATUS codes an NLA login fails with when the credentials are the problem.
            bool IsCredentialsStatus(uint32_t ntStatus)
            {
                switch (ntStatus)
                {
                    case 0xC000006D: // STATUS_LOGON_FAILURE
                    case 0xC000006A: // STATUS_WRONG_PASSWORD
                    case 0xC0000064: // STATUS_NO_SUCH_USER
                    case 0xC0000022: // STATUS_ACCESS_DENIED
                        return true;
                    default:
                        return false;
                }
            }

            std::string NtStatusText(uint32_t ntStatus)
            {
                switch (ntStatus)
                {
                    case 0xC000006D:
                    case 0xC000006A:
                    case 0xC0000064:
                    case 0xC0000022:
                        return "The user name or password was not accepted.";
                    case 0xC0000234:
                        return "That account is locked out.";
                    case 0xC0000071:
                        return "That account's password has expired.";
                    case 0xC0000072:
                        return "That account is disabled.";
                    case 0xC000006F:
                        return "That account isn't allowed to log in at this time.";
                    case 0xC0000070:
                        return "That account isn't allowed to log in from this computer.";
                    case 0xC0000193:
                        return "That account has expired.";
                    default:
                        break;
                }
                char code[32];
                snprintf(code, sizeof(code), "0x%08x", ntStatus);
                return std::string("The server rejected the login (status ") + code + ").";
            }

            // "DOMAIN\user" splits into the two; anything else is the user name as given.
            void SplitDomain(const std::string& account, std::string& domain, std::string& user)
            {
                size_t slash = account.find('\\');
                if (slash == std::string::npos)
                {
                    domain.clear();
                    user = account;
                }
                else
                {
                    domain = account.substr(0, slash);
                    user = account.substr(slash + 1);
                }
            }

            // Runs a function when it goes out of scope.
            struct ScopeExit
            {
                std::function<void()> run;
                ~ScopeExit() { run(); }
            };

            // A Disconnect Provider Ultimatum (MCS choice 8): the server is closing the connection.
            bool IsDisconnectUltimatum(const Bytes& mcsPdu)
            {
                return !mcsPdu.empty() && (mcsPdu[0] >> 2) == 8;
            }

            // The UTF-16 code unit for a keysym with no scancode, or 0 when it isn't a character.
            uint16_t KeysymToUtf16(uint32_t keysym)
            {
                if ((keysym >= 0x20 && keysym <= 0x7E) || (keysym >= 0xA0 && keysym <= 0xFF))
                {
                    return static_cast<uint16_t>(keysym); // Latin-1 keysyms are their own Unicode value
                }
                if ((keysym & 0xFF000000) == 0x01000000) // the Unicode keysym range
                {
                    uint32_t ucs = keysym & 0x00FFFFFF;
                    return ucs < 0x10000 ? static_cast<uint16_t>(ucs) : 0;
                }
                return 0;
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
            Rfb::Credentials credentials = options.credentials;
            if (credentials.password.empty() && !m_stopping)
            {
                // RDP always logs in with an account on the other computer: ask before connecting, so the login
                // doesn't fail first.
                if (!m_listener.OnCredentialsRequired(Rfb::CredentialsKind::Account, credentials))
                {
                    m_listener.OnDisconnected(info); // cancelled: no error
                    return;
                }
            }

            std::shared_ptr<Connection> shared = std::make_shared<Connection>();
            Connection& connection = *shared;
            connection.createTlsStream = options.createTlsStream;
            {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                m_connection = shared;
            }
            ScopeExit clearConnection{[this]() {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                m_connection.reset();
            }};

            CredSspCredentials nla;
            SplitDomain(credentials.username, nla.domain, nla.user);
            nla.password = credentials.password;
            std::vector<uint8_t> pinned = options.trustedCertificateSha256;
            auto trust = [this, pinned](const Net::TlsCertificate& certificate) {
                if (!pinned.empty() && certificate.sha256 == pinned)
                {
                    return true;
                }
                return m_listener.OnUntrustedCertificate(certificate);
            };
            std::string error;
            if (!connection.Open(options.host, options.port, nla, trust, options.connectTimeoutMs, error,
                                 &m_stopping))
            {
                const ConnectOutcome& outcome = connection.Outcome();
                if (m_stopping)
                {
                    info.error.clear();
                }
                else if (outcome.ntStatus != 0)
                {
                    info.error = NtStatusText(outcome.ntStatus);
                    info.authenticationFailed = IsCredentialsStatus(outcome.ntStatus);
                }
                else
                {
                    info.error = error;
                }
                m_listener.OnDisconnected(info);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_encryptionName = connection.Outcome().tlsName;
            }
            m_encrypted = connection.Outcome().encrypted;

            LogonInfo logon;
            logon.domain = nla.domain;
            logon.user = nla.user;
            logon.password = nla.password;
            if (!connection.Activate(options.desktopWidth, options.desktopHeight, options.clientName, logon, error))
            {
                info.error = m_stopping ? "" : error;
                m_encrypted = false;
                m_listener.OnDisconnected(info);
                return;
            }
            const ConnectOutcome& outcome = connection.Outcome();
            int width = outcome.desktopWidth != 0 ? outcome.desktopWidth : options.desktopWidth;
            int height = outcome.desktopHeight != 0 ? outcome.desktopHeight : options.desktopHeight;
            m_framebuffer.Resize(width, height);
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_shareId = outcome.shareId;
                m_userChannelId = connection.UserChannelId();
                m_buttons = 0;
                m_desktopWidth = width;
                m_desktopHeight = height;
            }
            m_palette.clear();
            m_lastErrorInfo.clear();
            m_decodeWarnings = 0;
            connection.GetSocket().SetReceiveTimeout(0); // the session waits for the server as long as it takes
            m_connected = true;
            Rfb::ServerInit serverInit;
            serverInit.width = width;
            serverInit.height = height;
            serverInit.name = options.host;
            m_listener.OnConnected(serverInit);
            LOG_INFO("Rdp", "Session with %s is up: desktop %dx%d, %s%s", options.host.c_str(), width, height,
                     outcome.tlsName.c_str(), outcome.usedNla ? " (NLA)" : "");

            SessionLoop(connection, info);

            m_connected = false;
            m_encrypted = false;
            m_listener.OnDisconnected(info);
        }

        void Client::SessionLoop(Connection& connection, DisconnectInfo& info)
        {
            info.wasConnected = true;
            bool finished = false;
            while (!m_stopping && !finished)
            {
                Bytes payload;
                bool fastPath = false;
                std::string error;
                if (!connection.ReadPdu(payload, fastPath, error))
                {
                    if (!m_stopping)
                    {
                        info.error = !m_lastErrorInfo.empty() ? m_lastErrorInfo : error;
                    }
                    return;
                }
                if (fastPath)
                {
                    HandleFastPath(payload);
                }
                else
                {
                    HandleMcsPdu(connection, payload, info, finished);
                }
            }
            if (m_stopping)
            {
                info.error.clear(); // a deliberate disconnect
            }
        }

        void Client::HandleMcsPdu(Connection& connection, const Bytes& mcsPdu, DisconnectInfo& info, bool& finished)
        {
            if (IsDisconnectUltimatum(mcsPdu))
            {
                info.error = !m_lastErrorInfo.empty() ? m_lastErrorInfo : "The server ended the connection.";
                finished = true;
                return;
            }
            uint16_t channel = 0;
            Bytes pdu;
            if (!StripSendData(mcsPdu, channel, pdu))
            {
                return; // not a data message: nothing this build acts on
            }
            ShareControlType type = ShareControlType::Data;
            uint16_t source = 0;
            Bytes body;
            if (!ReadShareControl(pdu, type, source, body))
            {
                return;
            }
            if (type == ShareControlType::DemandActive)
            {
                // The server is changing its desktop (a new size, a session switch): agree again.
                DemandActiveInfo demand;
                if (!ParseDemandActiveInfo(pdu, demand))
                {
                    return;
                }
                int width = demand.desktopWidth != 0 ? demand.desktopWidth : m_framebuffer.GetWidth();
                int height = demand.desktopHeight != 0 ? demand.desktopHeight : m_framebuffer.GetHeight();
                ResizeDesktop(width, height);
                std::string error;
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_shareId = demand.shareId;
                if (!connection.SendActivation(demand.shareId, static_cast<uint16_t>(width),
                                               static_cast<uint16_t>(height), error))
                {
                    LOG_WARNING("Rdp", "Couldn't answer the server's new Demand Active: %s", error.c_str());
                }
                return;
            }
            if (type != ShareControlType::Data)
            {
                return; // Deactivate All: a Demand Active follows, which is handled above
            }
            ShareDataType type2;
            Bytes data;
            if (!ReadShareData(pdu, type2, data))
            {
                return;
            }
            std::string error;
            switch (type2)
            {
                case ShareDataType::Update:
                {
                    Update update;
                    if (!ParseUpdatePdu(data, update, error))
                    {
                        LOG_WARNING("Rdp", "Ignoring an update the server sent: %s", error.c_str());
                        return;
                    }
                    if (update.type == UpdateType::Bitmap)
                    {
                        ApplyBitmapUpdate(update.rects);
                    }
                    else if (update.type == UpdateType::Palette)
                    {
                        m_palette = update.palette;
                    }
                    return;
                }
                case ShareDataType::Pointer:
                {
                    PointerUpdate pointer;
                    if (!ParsePointerPdu(data, pointer, error))
                    {
                        LOG_WARNING("Rdp", "Ignoring a pointer update the server sent: %s", error.c_str());
                        return;
                    }
                    ApplyPointerUpdate(pointer);
                    return;
                }
                default:
                    break;
            }
            uint8_t rawType = static_cast<uint8_t>(type2);
            if (rawType == kPduTypeSetErrorInfo)
            {
                uint32_t errorInfo = 0;
                if (ParseSetErrorInfo(data, errorInfo) && errorInfo != 0)
                {
                    m_lastErrorInfo = ErrorInfoText(errorInfo);
                    LOG_INFO("Rdp", "The server sent error info 0x%08x: %s", errorInfo, m_lastErrorInfo.c_str());
                }
            }
            else if (rawType == kPduTypeSaveSessionInfo)
            {
                LOG_INFO("Rdp", "The server reports the login succeeded");
            }
        }

        void Client::HandleFastPath(const Bytes& payload)
        {
            std::vector<FastPathUpdate> updates;
            std::string error;
            if (!ParseFastPathUpdates(payload, updates, error))
            {
                LOG_WARNING("Rdp", "Ignoring a fast-path update the server sent: %s", error.c_str());
                return;
            }
            for (const FastPathUpdate& update : updates)
            {
                switch (update.type)
                {
                    case FastPathUpdateType::Bitmap:
                        ApplyBitmapUpdate(update.rects);
                        break;
                    case FastPathUpdateType::Palette:
                        m_palette = update.palette;
                        break;
                    case FastPathUpdateType::Pointer:
                    {
                        PointerUpdate pointer;
                        if (ParseFastPathPointer(update.code, update.data, pointer, error))
                        {
                            ApplyPointerUpdate(pointer);
                        }
                        else
                        {
                            LOG_WARNING("Rdp", "Ignoring a pointer update the server sent: %s", error.c_str());
                        }
                        break;
                    }
                    case FastPathUpdateType::Other:
                        break;
                }
            }
        }

        void Client::ApplyBitmapUpdate(const std::vector<BitmapRect>& rects)
        {
            std::vector<Rfb::Rect> changed;
            std::vector<uint32_t> pixels;
            const uint32_t* palette = m_palette.size() == 256 ? m_palette.data() : nullptr;
            for (const BitmapRect& rect : rects)
            {
                std::string error;
                bool ok = rect.compressed
                              ? DecodeInterleavedRle(rect.data.data(), rect.data.size(), rect.width, rect.height,
                                                     rect.bitsPerPixel, palette, pixels, error)
                              : DecodeUncompressedBitmap(rect.data.data(), rect.data.size(), rect.width,
                                                         rect.height, rect.bitsPerPixel, palette, pixels, error);
                if (!ok)
                {
                    if (m_decodeWarnings++ < 5)
                    {
                        LOG_WARNING("Rdp", "Couldn't decode a %ux%u %u-bit %s rectangle at %u,%u: %s", rect.width,
                                    rect.height, rect.bitsPerPixel, rect.compressed ? "compressed" : "plain", rect.x,
                                    rect.y, error.c_str());
                    }
                    continue;
                }
                // Draw the part of the bitmap that fits its destination and the desktop.
                Rfb::Rect dest{rect.x, rect.y, std::min<int>(rect.width, rect.destWidth),
                               std::min<int>(rect.height, rect.destHeight)};
                {
                    std::lock_guard<std::mutex> lock(m_framebuffer.GetMutex());
                    dest.width = std::min(dest.width, m_framebuffer.GetWidth() - dest.x);
                    dest.height = std::min(dest.height, m_framebuffer.GetHeight() - dest.y);
                    if (dest.width <= 0 || dest.height <= 0 || !m_framebuffer.Contains(dest))
                    {
                        continue;
                    }
                    m_framebuffer.PutPixels(dest, pixels.data(), rect.width);
                }
                changed.push_back(dest);
            }
            if (!changed.empty())
            {
                ++m_statUpdates;
                m_statRectangles += static_cast<int>(changed.size());
                m_listener.OnFramebufferUpdated(changed);
            }
        }

        void Client::ApplyPointerUpdate(const PointerUpdate& update)
        {
            switch (update.kind)
            {
                case PointerUpdateKind::Hidden:
                    m_listener.OnCursorShape(CursorShape());
                    break;
                case PointerUpdateKind::Default:
                    m_listener.OnCursorShape(DefaultArrowCursor());
                    break;
                case PointerUpdateKind::New:
                    if (update.cacheIndex < kPointerCacheSize)
                    {
                        m_pointerCache[update.cacheIndex] = update.shape;
                    }
                    m_listener.OnCursorShape(update.shape);
                    break;
                case PointerUpdateKind::Cached:
                    if (update.cacheIndex < kPointerCacheSize && m_pointerCache[update.cacheIndex].width > 0)
                    {
                        m_listener.OnCursorShape(m_pointerCache[update.cacheIndex]);
                    }
                    break;
                case PointerUpdateKind::Position:
                case PointerUpdateKind::Other:
                    break;
            }
        }

        void Client::ResizeDesktop(int width, int height)
        {
            if (width == m_framebuffer.GetWidth() && height == m_framebuffer.GetHeight())
            {
                return;
            }
            m_framebuffer.Resize(width, height);
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_desktopWidth = width;
                m_desktopHeight = height;
            }
            m_listener.OnDesktopResized(width, height);
        }

        bool Client::SendPdu(const Bytes& pdu)
        {
            // m_sendMutex is held by the caller. The connection pointer is copied out so the write happens without
            // m_connectionMutex held: Disconnect() must be able to shut the socket down while a write is blocked.
            std::shared_ptr<Connection> connection;
            {
                std::lock_guard<std::mutex> lock(m_connectionMutex);
                connection = m_connection;
            }
            if (!connection || !m_connected)
            {
                return false;
            }
            std::string error;
            return connection->SendPdu(pdu, error);
        }

        void Client::SendInputEvents(const std::vector<Bytes>& events)
        {
            if (events.empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_sendMutex);
            SendPdu(BuildInputPdu(m_shareId, m_userChannelId, events));
        }

        void Client::SendPointerEvent(int x, int y, uint8_t buttonMask)
        {
            MarkInput();
            if (!m_connected)
            {
                return;
            }
            uint16_t px = static_cast<uint16_t>(std::max(0, x));
            uint16_t py = static_cast<uint16_t>(std::max(0, y));
            std::vector<Bytes> events;
            std::lock_guard<std::mutex> lock(m_sendMutex);
            events.push_back(MouseEvent(kPtrMove, px, py));
            // VNC's button mask: bit 0 left, 1 middle, 2 right, 3 wheel up, 4 wheel down, 5 wheel left, 6 right.
            struct Button
            {
                uint8_t bit;
                uint16_t flag;
            };
            static const Button kButtons[] = {{0x01, kPtrButton1}, {0x02, kPtrButton3}, {0x04, kPtrButton2}};
            for (const Button& button : kButtons)
            {
                bool was = (m_buttons & button.bit) != 0;
                bool now = (buttonMask & button.bit) != 0;
                if (was != now)
                {
                    events.push_back(MouseEvent(static_cast<uint16_t>(button.flag | (now ? kPtrDown : 0)), px, py));
                }
            }
            // Wheel "buttons" are pressed and released around each notch: send one turn on the press.
            constexpr uint16_t kPtrHWheel = 0x0400;
            if ((buttonMask & 0x08) && !(m_buttons & 0x08))
            {
                events.push_back(MouseEvent(kPtrWheel | 0x0078, px, py));
            }
            if ((buttonMask & 0x10) && !(m_buttons & 0x10))
            {
                events.push_back(MouseEvent(kPtrWheel | kPtrWheelNegative | 0x0088, px, py));
            }
            if ((buttonMask & 0x20) && !(m_buttons & 0x20))
            {
                events.push_back(MouseEvent(kPtrHWheel | kPtrWheelNegative | 0x0088, px, py));
            }
            if ((buttonMask & 0x40) && !(m_buttons & 0x40))
            {
                events.push_back(MouseEvent(kPtrHWheel | 0x0078, px, py));
            }
            m_buttons = buttonMask;
            ++m_statPointerEvents;
            SendPdu(BuildInputPdu(m_shareId, m_userChannelId, events));
        }

        void Client::SendKeyEvent(uint32_t keysym, bool down)
        {
            MarkInput();
            if (!m_connected)
            {
                return;
            }
            Scancode scancode = KeysymToScancode(keysym);
            Bytes event;
            if (scancode.known)
            {
                event = ScancodeEvent(scancode, down);
            }
            else
            {
                uint16_t unit = KeysymToUtf16(keysym);
                if (unit == 0)
                {
                    return; // nothing RDP can carry for this key
                }
                event = UnicodeEvent(unit, down);
            }
            ++m_statKeyEvents;
            SendInputEvents({event});
        }

        void Client::SendClientCutText(const std::string& /*latin1Text*/)
        {
            // The clipboard rides the cliprdr channel, which is milestone 6: nothing to send yet.
        }

        bool Client::OfferClipboard(const Rfb::ClipboardContent& /*content*/)
        {
            return false;
        }

        void Client::RequestFullUpdate()
        {
            if (!m_connected)
            {
                return;
            }
            ++m_statRequests;
            std::lock_guard<std::mutex> lock(m_sendMutex);
            SendPdu(BuildRefreshRect(m_shareId, m_userChannelId, static_cast<uint16_t>(m_desktopWidth),
                                     static_cast<uint16_t>(m_desktopHeight)));
        }

        void Client::RequestDesktopSize(int /*width*/, int /*height*/)
        {
            // Resizing the other computer's desktop is milestone 7; SupportsDesktopResize() says no for now.
        }

        void Client::SetQuality(int /*compressLevel*/, int /*qualityLevel*/)
        {
            // The classic bitmap updates have no quality setting; the codecs that do are milestone 8.
        }

        std::string Client::GetEncryptionName() const
        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
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
            stats.requests = m_statRequests.exchange(0);
            stats.pointerEvents = m_statPointerEvents.exchange(0);
            stats.keyEvents = m_statKeyEvents.exchange(0);
            return stats;
        }
    } // namespace Rdp
} // namespace Marconi
