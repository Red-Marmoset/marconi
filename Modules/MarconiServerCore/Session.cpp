// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The protocol session

#include "Session.h"

#include <MarconiCore/Wire/WireCompress.h>

#include <Log/Log.h>

#include <chrono>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // What this build's server can do, for the capability handshake. It grows as phases land (video,
            // clipboard, files, gamepads); for now it can describe several displays and nothing more.
            constexpr uint64_t kServerCapabilities = Wire::Capability::MultipleDisplays;

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
            m_stop = true;
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
            LOG_INFO("Server", "Viewer '%s' connected, protocol %u", hello.clientName.c_str(),
                     static_cast<unsigned>(hello.version));

            Wire::Welcome welcome;
            welcome.version = Wire::kProtocolVersion;
            const uint64_t granted = kServerCapabilities | (m_clipboard != nullptr ? Wire::Capability::Clipboard : 0);
            welcome.capabilities = hello.capabilities & granted;
            m_clipboardActive = (welcome.capabilities & Wire::Capability::Clipboard) != 0;
            welcome.serverName = m_settings.serverName;
            welcome.serverVersion = m_settings.serverVersion;
            welcome.displays = m_screen.GetDisplays();
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
                    case Wire::Type::Bye:
                        m_stop = true;
                        break;
                    default:
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
            Wire::FrameBegin begin;
            begin.frameId = ++m_frameId;
            begin.displayId = 0;
            if (!Send(begin.Encode()))
            {
                m_stop = true;
                return;
            }
            const std::vector<Wire::Rect>& rects = capture.dirty;
            Wire::Rect whole{0, 0, capture.width, capture.height};
            const std::vector<Wire::Rect> wholeOnly{whole};
            for (const Wire::Rect& rect : (full ? wholeOnly : rects))
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

        void Session::RunFrameLoop()
        {
            bool first = true;
            while (!m_stop)
            {
                if (m_clipboardActive && m_clipboardChanged.exchange(false))
                {
                    CheckClipboard();
                }
                Capture capture;
                if (m_screen.Grab(capture))
                {
                    if (first || !capture.dirty.empty())
                    {
                        SendFrame(capture, first);
                        first = false;
                    }
                }
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
        }
    } // namespace Server
} // namespace Marconi
