// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The protocol session

#include "Session.h"

#include "StillEncoder.h"

#include <Log/Log.h>

#include <chrono>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // What this build's server can do, for the capability handshake. It grows as phases land (video,
            // clipboard, files, gamepads); for now it can describe several displays and send timestamps.
            constexpr uint64_t kServerCapabilities = Wire::Capability::MultipleDisplays | Wire::Capability::Timestamps;
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
            welcome.capabilities = hello.capabilities & kServerCapabilities;
            m_grantedCapabilities = welcome.capabilities;
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
                uint64_t receivedMicros = 0;
                try
                {
                    bytes = reader.Next();
                    receivedMicros = Wire::NowMicros();
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
                        SendPong(Wire::Ping::Decode(body).token, receivedMicros);
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

        bool Session::SendPong(uint64_t token, uint64_t receivedMicros)
        {
            Wire::Pong pong;
            pong.token = token;
            std::lock_guard<std::mutex> lock(m_writeMutex);
            if (Timestamps())
            {
                // Stamped once the write lock is held, so time spent queued behind a picture counts as the
                // server's, not the network's, and the client's clock comparison stays fair.
                pong.hasTimes = true;
                pong.receivedMicros = receivedMicros;
                pong.answeredMicros = Wire::NowMicros();
            }
            return Wire::WriteMessage(m_transport, pong.Encode());
        }

        void Session::SendFrame(const Capture& capture, bool full, uint64_t captureMicros)
        {
            Wire::FrameBegin begin;
            begin.frameId = ++m_frameId;
            begin.displayId = 0;
            if (!Send(begin.Encode()))
            {
                m_stop = true;
                return;
            }
            uint64_t compressMicros = 0;
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
                StillTiming timing;
                EncodeStill(capture, rect, still, &timing);
                compressMicros += timing.packMicros + timing.compressMicros;
                if (!Send(still.Encode()))
                {
                    m_stop = true;
                    return;
                }
            }
            Wire::FrameEnd end;
            end.frameId = begin.frameId;
            if (Timestamps())
            {
                end.hasTimes = true;
                end.captureMicros = captureMicros;
                end.sentMicros = Wire::NowMicros();
                end.compressMicros = static_cast<uint32_t>(compressMicros);
            }
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
                // Stamped as the grab starts, so the time spent reading the screen counts towards the picture's age.
                const uint64_t captureMicros = Wire::NowMicros();
                if (m_screen.Grab(capture))
                {
                    if (first || !capture.dirty.empty())
                    {
                        SendFrame(capture, first, captureMicros);
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

            std::thread input([this, &reader]() { RunInputLoop(reader); });
            RunFrameLoop();
            m_stop = true;
            input.join();
        }
    } // namespace Server
} // namespace Marconi
