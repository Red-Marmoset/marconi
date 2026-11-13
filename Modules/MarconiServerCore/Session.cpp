// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The protocol session

#include "Session.h"

#include "FileService.h"

#include <MarconiCore/Wire/WireCompress.h>

#include <Log/Log.h>

#include <algorithm>
#include <chrono>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // What this build's server can always do, for the capability handshake. It grows as phases land (video,
            // clipboard, files, gamepads). Resizing a display is added when the screen source can do it.
            constexpr uint64_t kServerCapabilities = Wire::Capability::MultipleDisplays;

            Wire::Rect Intersect(const Wire::Rect& a, const Wire::Rect& b)
            {
                int32_t left = std::max(a.x, b.x);
                int32_t top = std::max(a.y, b.y);
                int32_t right = std::min(a.x + a.width, b.x + b.width);
                int32_t bottom = std::min(a.y + a.height, b.y + b.height);
                if (right <= left || bottom <= top)
                {
                    return {0, 0, 0, 0};
                }
                return {left, top, right - left, bottom - top};
            }

            bool SameLayout(const std::vector<Wire::Display>& a, const std::vector<Wire::Display>& b)
            {
                if (a.size() != b.size())
                {
                    return false;
                }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    if (a[i].id != b[i].id || a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width ||
                        a[i].height != b[i].height)
                    {
                        return false;
                    }
                }
                return true;
            }

            // Packs the pixels of one rectangle of a capture into a Still's little-endian 0x00RRGGBB bytes
            // (byte order B, G, R, 0), row by row.
            Wire::Bytes PackRect(const Capture& capture, const Wire::Rect& rect)
            {
                Wire::Bytes data;
                data.reserve(static_cast<size_t>(rect.width) * rect.height * 4);
                for (int32_t y = rect.y; y < rect.y + rect.height; ++y)
                {
                    const uint32_t* row = capture.Row(y);
                    for (int32_t x = rect.x; x < rect.x + rect.width; ++x)
                    {
                        uint32_t pixel = row[x];
                        data.push_back(static_cast<uint8_t>(pixel));
                        data.push_back(static_cast<uint8_t>(pixel >> 8));
                        data.push_back(static_cast<uint8_t>(pixel >> 16));
                        data.push_back(0);
                    }
                }
                return data;
            }

            bool RectInside(const Wire::Rect& rect, int32_t width, int32_t height)
            {
                return rect.width > 0 && rect.height > 0 && rect.x >= 0 && rect.y >= 0 &&
                       rect.x + rect.width <= width && rect.y + rect.height <= height;
            }
        } // namespace

        Session::Session(Wire::Transport& transport, ScreenSource& screen, InputSink& input, const Settings& settings,
                         Auth* auth)
            : m_transport(transport), m_screen(screen), m_input(input), m_settings(settings), m_auth(auth)
        {
        }

        void Session::Stop()
        {
            m_stoppedByServer = true; // the server is ending this viewer, so it isn't a "viewer disconnected" lock
            m_stop = true;
        }

        std::string Session::GetViewerName() const
        {
            std::lock_guard<std::mutex> lock(m_nameMutex);
            return m_viewerName;
        }

        void Session::LockComputer()
        {
            if (m_locked.exchange(true))
            {
                return; // already locked for this viewer
            }
            if (m_input.LockWorkstation())
            {
                LOG_INFO("Server", "Locked this computer's screen");
            }
            else
            {
                LOG_INFO("Server", "Could not lock this computer's screen (no way to lock here, or it failed)");
            }
        }

        bool Session::Send(const Wire::Bytes& framed)
        {
            std::lock_guard<std::mutex> lock(m_writeMutex);
            return Wire::WriteMessage(m_transport, framed);
        }

        bool Session::Handshake(Wire::MessageReader& reader)
        {
            Wire::Bytes helloBytes = reader.Next();
            Wire::Reader helloReader(helloBytes);
            if (Wire::ReadType(helloReader) != Wire::Type::Hello)
            {
                LOG_INFO("Server", "A client sent something other than Hello first; dropping it");
                return false;
            }
            Wire::Hello hello = Wire::Hello::Decode(helloReader);
            m_clientCapabilities = hello.capabilities;
            {
                std::lock_guard<std::mutex> lock(m_nameMutex);
                m_viewerName = hello.clientName;
            }
            LOG_INFO("Server", "Viewer '%s' connected, protocol %u", hello.clientName.c_str(),
                     static_cast<unsigned>(hello.version));

            Wire::Welcome welcome;
            welcome.version = Wire::kProtocolVersion;
            uint64_t offered = kServerCapabilities;
            if (m_settings.filesEnabled)
            {
                offered |= Wire::Capability::Files;
            }
            if (m_clipboard != nullptr)
            {
                offered |= Wire::Capability::Clipboard;
            }
            if (m_screen.CanSetDisplayMode())
            {
                offered |= Wire::Capability::ResizeDisplay;
            }
            m_grantedCapabilities = hello.capabilities & offered;
            welcome.capabilities = m_grantedCapabilities;
            m_clipboardActive = (welcome.capabilities & Wire::Capability::Clipboard) != 0;
            welcome.serverName = m_settings.serverName;
            welcome.serverVersion = m_settings.serverVersion;
            welcome.displays = m_screen.GetDisplays();
            m_displays = welcome.displays;
            if (!Send(welcome.Encode()))
            {
                return false;
            }

            Wire::Bytes loginBytes = reader.Next();
            Wire::Reader loginReader(loginBytes);
            if (Wire::ReadType(loginReader) != Wire::Type::Login)
            {
                LOG_INFO("Server", "Viewer skipped the login; dropping it");
                return false;
            }
            Wire::Login login = Wire::Login::Decode(loginReader);

            Wire::LoginResult result;
            result.status = Wire::LoginResult::Status::Ok;
            if (m_auth != nullptr)
            {
                int retryAfterSeconds = 0;
                Auth::Result check = m_auth->Check(login.password, retryAfterSeconds);
                if (check == Auth::Result::WrongPassword)
                {
                    result.status = Wire::LoginResult::Status::WrongPassword;
                    LOG_INFO("Server", "Viewer '%s' gave the wrong password", hello.clientName.c_str());
                }
                else if (check == Auth::Result::LockedOut)
                {
                    result.status = Wire::LoginResult::Status::LockedOut;
                    result.retryAfterSeconds = static_cast<uint32_t>(retryAfterSeconds);
                    LOG_INFO("Server", "Login locked out for %d seconds", retryAfterSeconds);
                }
            }
            if (!Send(result.Encode()) || result.status != Wire::LoginResult::Status::Ok)
            {
                return false; // the viewer isn't allowed in (or the connection failed)
            }
            m_loggedIn = true;
            return true;
        }

        void Session::RunInputLoop(Wire::MessageReader& reader)
        {
            while (!m_stop)
            {
                Wire::Bytes bytes;
                try
                {
                    bytes = reader.Next();
                }
                catch (const Wire::WireError&)
                {
                    break; // the viewer went away or sent nonsense
                }
                Wire::Reader body(bytes);
                Wire::Type type;
                try
                {
                    type = Wire::ReadType(body);
                    switch (type)
                    {
                    case Wire::Type::Pointer:
                    {
                        Wire::Pointer p = Wire::Pointer::Decode(body);
                        m_input.MovePointer(p.x, p.y, p.buttons, p.wheelHigh, p.wheelHighX);
                        break;
                    }
                    case Wire::Type::Key:
                    {
                        Wire::Key k = Wire::Key::Decode(body);
                        m_input.SendKey(k.keysym, k.scancode, k.down);
                        break;
                    }
                    case Wire::Type::Text:
                        m_input.TypeText(Wire::Text::Decode(body).utf8);
                        break;
                    case Wire::Type::SelectDisplay:
                    {
                        Wire::SelectDisplay select = Wire::SelectDisplay::Decode(body);
                        std::lock_guard<std::mutex> lock(m_requestMutex);
                        m_requestedDisplay = select.displayId;
                        m_selectionPending = true;
                        break;
                    }
                    case Wire::Type::SetDisplayMode:
                    {
                        Wire::SetDisplayMode mode = Wire::SetDisplayMode::Decode(body);
                        if ((m_grantedCapabilities & Wire::Capability::ResizeDisplay) == 0)
                        {
                            Wire::DisplayModeResult refused;
                            refused.displayId = mode.displayId;
                            refused.status = Wire::DisplayModeResult::Status::NotAllowed;
                            Send(refused.Encode());
                            break;
                        }
                        std::lock_guard<std::mutex> lock(m_requestMutex);
                        m_requestedMode = mode;
                        m_modePending = true;
                        break;
                    }
                    case Wire::Type::Ping:
                    {
                        Wire::Pong pong;
                        pong.token = Wire::Ping::Decode(body).token;
                        Send(pong.Encode());
                        break;
                    }
                    case Wire::Type::ClipboardOffer:
                        if (m_clipboardActive)
                        {
                            HandleClipboardOffer(Wire::ClipboardOffer::Decode(body));
                        }
                        break;
                    case Wire::Type::ClipboardRequest:
                        if (m_clipboardActive)
                        {
                            HandleClipboardRequest(Wire::ClipboardRequest::Decode(body));
                        }
                        break;
                    case Wire::Type::ClipboardData:
                        if (m_clipboardActive)
                        {
                            HandleClipboardData(Wire::ClipboardData::Decode(body));
                        }
                        break;
                    case Wire::Type::Lock:
                        Wire::Lock::Decode(body);
                        LockComputer();
                        break;
                    case Wire::Type::Bye:
                        m_stop = true;
                        break;
                    default:
                        if (m_files != nullptr)
                        {
                            m_files->Handle(type, body); // file messages; anything else it leaves
                        }
                        break; // a message this build doesn't act on yet: its length already let us skip it
                    }
                }
                catch (const Wire::WireError&)
                {
                    break; // a malformed message: drop the viewer
                }
            }
            m_stop = true;
        }

        namespace
        {
            uint64_t HashClipboard(const ClipboardContents& contents)
            {
                uint64_t hash = 1469598103934665603ull; // FNV-1a
                auto mix = [&hash](const uint8_t* data, size_t length) {
                    for (size_t i = 0; i < length; ++i)
                    {
                        hash = (hash ^ data[i]) * 1099511628211ull;
                    }
                };
                mix(reinterpret_cast<const uint8_t*>(contents.text.data()), contents.text.size());
                const uint8_t separator = 0xFF;
                mix(&separator, 1);
                mix(contents.png.data(), contents.png.size());
                return hash;
            }

            // Drops what is too big to carry, so an oversize clipboard is simply not shared.
            void LimitClipboard(ClipboardContents& contents)
            {
                if (contents.text.size() > Wire::kMaxClipboardTextBytes)
                {
                    contents.text.clear();
                }
                if (contents.png.size() > Wire::kMaxClipboardImageBytes)
                {
                    contents.png.clear();
                }
            }
        } // namespace

        bool Session::RememberClipboard(const ClipboardContents& contents, bool alsoKnown)
        {
            uint64_t hash = HashClipboard(contents);
            std::lock_guard<std::mutex> lock(m_clipboardMutex);
            if (alsoKnown)
            {
                m_recentHashes.push_back(hash); // another form of what is already known
                return true;
            }
            for (uint64_t known : m_recentHashes)
            {
                if (known == hash)
                {
                    return false;
                }
            }
            m_recentHashes.assign(1, hash); // a new content: only it counts as already exchanged now
            return true;
        }

        void Session::CheckClipboard()
        {
            ClipboardContents contents;
            if (!m_clipboard->Read(contents))
            {
                return;
            }
            LimitClipboard(contents);
            if (contents.IsEmpty() || !RememberClipboard(contents))
            {
                return; // nothing to share, or the content we just wrote ourselves (or already offered)
            }
            Wire::ClipboardOffer offer;
            offer.hasText = !contents.text.empty();
            offer.hasImage = !contents.png.empty();
            if (!Send(offer.Encode()))
            {
                m_stop = true;
            }
        }

        void Session::HandleClipboardOffer(const Wire::ClipboardOffer& offer)
        {
            {
                std::lock_guard<std::mutex> lock(m_clipboardMutex);
                m_incoming = ClipboardContents();
                m_awaitText = offer.hasText;
                m_awaitImage = offer.hasImage;
            }
            // Ask for what is offered. The viewer answers each request with its data.
            if (offer.hasText)
            {
                Wire::ClipboardRequest request;
                request.format = Wire::ClipboardRequest::Format::Text;
                Send(request.Encode());
            }
            if (offer.hasImage)
            {
                Wire::ClipboardRequest request;
                request.format = Wire::ClipboardRequest::Format::Image;
                Send(request.Encode());
            }
        }

        void Session::HandleClipboardRequest(const Wire::ClipboardRequest& request)
        {
            // Always answer, with nothing when there is nothing (or it is too big), so the viewer isn't left waiting.
            ClipboardContents contents;
            m_clipboard->Read(contents);
            LimitClipboard(contents);
            RememberClipboard(contents); // the viewer now has it: no offer for it later
            Wire::ClipboardData data;
            data.format = request.format;
            data.data = request.format == Wire::ClipboardRequest::Format::Text
                            ? Wire::Bytes(contents.text.begin(), contents.text.end())
                            : contents.png;
            if (!Send(data.Encode()))
            {
                m_stop = true;
            }
        }

        void Session::HandleClipboardData(Wire::ClipboardData&& data)
        {
            ClipboardContents ready;
            {
                std::lock_guard<std::mutex> lock(m_clipboardMutex);
                const bool isText = data.format == Wire::ClipboardRequest::Format::Text;
                bool& awaited = isText ? m_awaitText : m_awaitImage;
                if (!awaited)
                {
                    return; // nothing was asked for: ignore it
                }
                awaited = false;
                if (isText && data.data.size() <= Wire::kMaxClipboardTextBytes)
                {
                    m_incoming.text.assign(data.data.begin(), data.data.end());
                }
                else if (!isText && data.data.size() <= Wire::kMaxClipboardImageBytes)
                {
                    m_incoming.png = std::move(data.data);
                }
                if (m_awaitText || m_awaitImage)
                {
                    return; // the other format is still on its way
                }
                ready = std::move(m_incoming);
                m_incoming = ClipboardContents();
            }
            if (ready.IsEmpty())
            {
                return;
            }
            // Remember it before writing, so the change this causes is recognised as ours.
            RememberClipboard(ready);
            if (!m_clipboard->Write(ready))
            {
                LOG_INFO("Server", "Could not set this computer's clipboard from the viewer");
                return;
            }
            ClipboardContents back; // what the platform now reports (it may differ in form from what we wrote)
            if (m_clipboard->Read(back))
            {
                RememberClipboard(back, true);
            }
        }

        void Session::SendFrame(const Capture& capture, bool full)
        {
            // Only the chosen display is streamed (the whole desktop when none is chosen): everything is clipped to
            // it, so a viewer looking at one display isn't sent the others. Coordinates stay desktop coordinates.
            Wire::Rect area{0, 0, capture.width, capture.height};
            if (m_selectedDisplay != 0)
            {
                for (const Wire::Display& display : m_displays)
                {
                    if (display.id == m_selectedDisplay)
                    {
                        area = Intersect(area, {display.x, display.y, display.width, display.height});
                    }
                }
            }
            std::vector<Wire::Rect> rects;
            if (full)
            {
                rects.push_back(area);
            }
            else
            {
                for (const Wire::Rect& rect : capture.dirty)
                {
                    Wire::Rect clipped = Intersect(rect, area);
                    if (clipped.width > 0 && clipped.height > 0)
                    {
                        rects.push_back(clipped);
                    }
                }
            }
            if (rects.empty())
            {
                return; // everything that changed is on a display the viewer isn't watching
            }

            Wire::FrameBegin begin;
            begin.frameId = ++m_frameId;
            begin.displayId = m_selectedDisplay;
            if (!Send(begin.Encode()))
            {
                m_stop = true;
                return;
            }
            for (const Wire::Rect& rect : rects)
            {
                if (!RectInside(rect, capture.width, capture.height))
                {
                    continue;
                }
                Wire::Still still;
                still.rect = rect;
                Wire::Bytes raw = PackRect(capture, rect);
                // Compress unless zlib can't beat the raw pixels (tiny or already-random rectangles): a screen is
                // mostly flat colour and text, so this usually sends far less, which is what keeps the link lean.
                Wire::Bytes packed = Wire::ZlibCompress(raw);
                if (!packed.empty() && packed.size() < raw.size())
                {
                    still.compression = Wire::Still::Compression::Zlib;
                    still.data = std::move(packed);
                }
                else
                {
                    still.compression = Wire::Still::Compression::None;
                    still.data = std::move(raw);
                }
                if (!Send(still.Encode()))
                {
                    m_stop = true;
                    return;
                }
            }
            Wire::FrameEnd end;
            end.frameId = begin.frameId;
            if (!Send(end.Encode()))
            {
                m_stop = true;
            }
        }

        bool Session::CheckLayout(bool force)
        {
            auto now = std::chrono::steady_clock::now();
            if (!force && now - m_lastLayoutCheck < std::chrono::milliseconds(500))
            {
                return false;
            }
            m_lastLayoutCheck = now;
            std::vector<Wire::Display> displays = m_screen.GetDisplays();
            if (displays.empty() || SameLayout(displays, m_displays))
            {
                return false;
            }
            m_displays = displays;
            Wire::Displays message;
            message.displays = displays;
            if (!Send(message.Encode()))
            {
                m_stop = true;
                return false;
            }
            if (m_selectedDisplay != 0 &&
                std::none_of(displays.begin(), displays.end(),
                             [this](const Wire::Display& d) { return d.id == m_selectedDisplay; }))
            {
                m_selectedDisplay = 0; // the chosen display went away: back to the whole desktop
            }
            m_needFull = true; // the picture's size or place changed: send it again whole
            return true;
        }

        void Session::ApplyPendingRequests()
        {
            bool selectionPending;
            uint32_t requestedDisplay;
            bool modePending;
            Wire::SetDisplayMode mode;
            {
                std::lock_guard<std::mutex> lock(m_requestMutex);
                selectionPending = m_selectionPending;
                requestedDisplay = m_requestedDisplay;
                modePending = m_modePending;
                mode = m_requestedMode;
                m_selectionPending = false;
                m_modePending = false;
            }
            if (selectionPending)
            {
                bool known = requestedDisplay == 0;
                for (const Wire::Display& display : m_displays)
                {
                    known = known || display.id == requestedDisplay;
                }
                if (known)
                {
                    m_selectedDisplay = requestedDisplay;
                    m_needFull = true; // show the chosen display whole straight away
                }
            }
            if (modePending)
            {
                // Id 0 means "the display being watched", or the first one when the whole desktop is.
                uint32_t id = mode.displayId;
                if (id == 0)
                {
                    id = m_selectedDisplay != 0 ? m_selectedDisplay : (m_displays.empty() ? 0 : m_displays[0].id);
                }
                bool known = false;
                for (const Wire::Display& display : m_displays)
                {
                    known = known || display.id == id;
                }
                Wire::DisplayModeResult result;
                result.displayId = id;
                result.status = known ? m_screen.SetDisplayMode(id, mode.width, mode.height)
                                      : Wire::DisplayModeResult::Status::NoSuchDisplay;
                if (!Send(result.Encode()))
                {
                    m_stop = true;
                    return;
                }
                if (result.status == Wire::DisplayModeResult::Status::Ok)
                {
                    CheckLayout(true);
                }
            }
        }

        void Session::SendCursor()
        {
            Cursor cursor;
            if (m_screen.GetCursor(cursor))
            {
                Wire::CursorShape shape;
                shape.width = cursor.width;
                shape.height = cursor.height;
                shape.hotspotX = cursor.hotspotX;
                shape.hotspotY = cursor.hotspotY;
                shape.argb.reserve(cursor.argb.size() * 4);
                for (uint32_t pixel : cursor.argb)
                {
                    shape.argb.push_back(static_cast<uint8_t>(pixel));
                    shape.argb.push_back(static_cast<uint8_t>(pixel >> 8));
                    shape.argb.push_back(static_cast<uint8_t>(pixel >> 16));
                    shape.argb.push_back(static_cast<uint8_t>(pixel >> 24));
                }
                if (cursor.argb.empty())
                {
                    shape.width = 0;
                    shape.height = 0;
                }
                if (!Send(shape.Encode()))
                {
                    m_stop = true;
                    return;
                }
            }
            int32_t x = 0;
            int32_t y = 0;
            if (m_screen.GetCursorPosition(x, y) && (x != m_lastCursorX || y != m_lastCursorY))
            {
                m_lastCursorX = x;
                m_lastCursorY = y;
                Wire::CursorPosition position;
                position.x = x;
                position.y = y;
                if (!Send(position.Encode()))
                {
                    m_stop = true;
                }
            }
        }

        void Session::RunFrameLoop()
        {
            bool first = true;
            int32_t lastWidth = 0;
            int32_t lastHeight = 0;
            m_lastLayoutCheck = std::chrono::steady_clock::now();
            while (!m_stop)
            {
                if (m_clipboardActive && m_clipboardChanged.exchange(false))
                {
                    CheckClipboard();
                }
                ApplyPendingRequests();
                CheckLayout(false);
                Capture capture;
                if (m_screen.Grab(capture))
                {
                    if (capture.width != lastWidth || capture.height != lastHeight)
                    {
                        // The desktop changed size under us (a resize, a monitor plugged in): tell the viewer the
                        // layout before the first picture of the new size.
                        if (!first)
                        {
                            CheckLayout(true);
                        }
                        lastWidth = capture.width;
                        lastHeight = capture.height;
                        m_needFull = true;
                    }
                    if (first || m_needFull || !capture.dirty.empty())
                    {
                        bool full = first || m_needFull;
                        m_needFull = false;
                        SendFrame(capture, full);
                        first = false;
                    }
                }
                SendCursor();
                if (m_frameIntervalMs > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(m_frameIntervalMs));
                }
            }
        }

        void Session::Run()
        {
            // The transport is already encrypted and past the four-byte marker (the listener writes that marker on
            // the raw socket and runs the TLS handshake before handing the session the TLS stream). The session is
            // pure protocol from here.
            Wire::MessageReader reader(m_transport);
            try
            {
                if (!Handshake(reader))
                {
                    return;
                }
            }
            catch (const Wire::WireError& error)
            {
                LOG_INFO("Server", "Handshake failed: %s", error.what());
                return;
            }

            m_files = std::make_unique<FileService>(m_settings.filesEnabled, m_settings.fileRoot,
                                                    [this](const Wire::Bytes& framed) { return Send(framed); });
            if (m_clipboardActive)
            {
                m_clipboard->SetChangeHandler([this]() { m_clipboardChanged = true; });
            }
            std::thread input([this, &reader]() { RunInputLoop(reader); });
            RunFrameLoop();
            m_stop = true;
            if (m_clipboardActive)
            {
                m_clipboard->SetChangeHandler(nullptr); // returns once no call to the handler is running
            }
            input.join();
            m_files->Stop();
            m_screen.RestoreDisplayModes(); // a changed screen size doesn't outlive the viewer
            // A viewer that got in and then went away (not one the server itself ended) locks the screen when the
            // setting says so.
            if (m_loggedIn && m_settings.lockOnDisconnect && !m_stoppedByServer)
            {
                LockComputer();
            }
        }
    } // namespace Server
} // namespace Marconi
