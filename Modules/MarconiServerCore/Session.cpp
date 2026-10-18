// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The protocol session

#include "Session.h"

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

        Session::Session(Wire::Transport& transport, ScreenSource& screen, InputSink& input, const Settings& settings)
            : m_transport(transport), m_screen(screen), m_input(input), m_settings(settings)
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
            welcome.capabilities = hello.capabilities & kServerCapabilities;
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
            Wire::Login::Decode(loginReader); // the password is checked once it travels inside TLS (a later step)

            Wire::LoginResult result;
            result.status = Wire::LoginResult::Status::Ok;
            return Send(result.Encode());
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
                still.compression = Wire::Still::Compression::None; // zlib arrives with the real capture
                still.data = PackRect(capture, rect);
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

            std::thread input([this, &reader]() { RunInputLoop(reader); });
            RunFrameLoop();
            m_stop = true;
            input.join();
        }
    } // namespace Server
} // namespace Marconi
