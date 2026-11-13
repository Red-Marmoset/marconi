// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Both ends of the control pipe; see ControlSession.h

#include "ControlSession.h"

#include <Log/Log.h>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            ControlSession::ControlSession(Wire::Transport& transport, ControlHost& host, const PeerInfo& peer,
                                           const std::string& serverVersion)
                : m_transport(transport), m_host(host), m_peer(peer), m_serverVersion(serverVersion)
            {
            }

            bool ControlSession::Send(const Wire::Bytes& framed)
            {
                std::lock_guard<std::mutex> lock(m_writeMutex);
                return Wire::WriteMessage(m_transport, framed);
            }

            void ControlSession::Reply(Type request, ResultCode code, const std::string& text)
            {
                Result result;
                result.request = request;
                result.code = code;
                result.text = text;
                Send(result.Encode());
            }

            bool ControlSession::PushStatus()
            {
                if (!m_ready)
                {
                    return false;
                }
                Status message;
                message.status = m_host.GetStatus();
                return Send(message.Encode());
            }

            bool ControlSession::Handle(Wire::Reader& body, Type type)
            {
                if (!m_ready)
                {
                    if (type != Type::Hello)
                    {
                        Reply(type, ResultCode::BadRequest, "Say Hello first.");
                        return false;
                    }
                    Hello hello = Hello::Decode(body);
                    if (hello.version != kVersion)
                    {
                        Reply(type, ResultCode::BadRequest, "This tray icon and the server are different versions.");
                        return false;
                    }
                    Welcome welcome;
                    welcome.serverVersion = m_serverVersion;
                    if (!Send(welcome.Encode()))
                    {
                        return false;
                    }
                    m_ready = true;
                    return PushStatus(); // the first Status comes without being asked for
                }

                switch (type)
                {
                case Type::Hello:
                    Reply(type, ResultCode::BadRequest, "Already said Hello.");
                    return false;
                case Type::GetStatus:
                {
                    GetStatus::Decode(body);
                    return PushStatus();
                }
                case Type::DisconnectViewer:
                {
                    DisconnectViewer request = DisconnectViewer::Decode(body);
                    std::string text;
                    ResultCode code = m_host.DisconnectViewer(request.viewerId, text);
                    Reply(type, code, text);
                    return true;
                }
                case Type::SetPassword:
                {
                    SetPassword request = SetPassword::Decode(body);
                    if (!m_peer.isAdministrator)
                    {
                        Reply(type, ResultCode::NotAdministrator,
                              "Changing the password needs administrator permission.");
                        return true; // not fatal: the tray icon asks for elevation and connects again
                    }
                    std::string text;
                    ResultCode code = m_host.SetPasswordHash(request.passwordHash, text);
                    Reply(type, code, text);
                    return true;
                }
                default:
                    return true; // a type this build doesn't know (or one only the helper sends): skipped
                }
            }

            void ControlSession::Run()
            {
                Wire::MessageReader reader(m_transport);
                while (!m_stop)
                {
                    Wire::Bytes bytes;
                    try
                    {
                        bytes = reader.Next();
                    }
                    catch (const Wire::WireError&)
                    {
                        break; // the tray icon went away
                    }
                    try
                    {
                        Wire::Reader body(bytes);
                        Type type = ReadType(body);
                        if (!Handle(body, type))
                        {
                            break;
                        }
                    }
                    catch (const Wire::WireError& error)
                    {
                        LOG_INFO("Service", "The tray icon sent a malformed message (%s); closing the pipe",
                                 error.what());
                        break;
                    }
                }
                m_ready = false;
            }

            // -- ControlClient -----------------------------------------------------------------------------------

            bool ControlClient::Send(const Wire::Bytes& framed)
            {
                return Wire::WriteMessage(m_transport, framed);
            }

            bool ControlClient::WaitFor(Type want, Wire::Bytes& bytes, std::string& error)
            {
                for (;;)
                {
                    try
                    {
                        bytes = m_reader.Next();
                        Wire::Reader body(bytes);
                        Type type = ReadType(body);
                        if (type == want)
                        {
                            return true;
                        }
                        if (type == Type::Status)
                        {
                            m_latest = Status::Decode(body).status;
                        }
                        else if (type == Type::Result && want != Type::Result)
                        {
                            // The helper refused something (for example Hello): that ends the wait with its reason.
                            error = Result::Decode(body).text;
                            m_refused = true;
                            if (error.empty())
                            {
                                error = "the server refused the request";
                            }
                            return false;
                        }
                        // anything else is a message this build doesn't know: skip it
                    }
                    catch (const Wire::WireError&)
                    {
                        error = "the connection to the server was lost";
                        return false;
                    }
                }
            }

            bool ControlClient::Connect(const std::string& clientName, std::string& error)
            {
                m_versionMismatch = false;
                m_refused = false;
                Hello hello;
                hello.clientName = clientName;
                if (!Send(hello.Encode()))
                {
                    error = "could not talk to the server";
                    return false;
                }
                Wire::Bytes bytes;
                if (!WaitFor(Type::Welcome, bytes, error))
                {
                    // The only thing the helper refuses a first Hello for is a version it doesn't speak.
                    m_versionMismatch = m_refused;
                    return false;
                }
                try
                {
                    Wire::Reader body(bytes);
                    ReadType(body);
                    Welcome welcome = Welcome::Decode(body);
                    if (welcome.version != kVersion)
                    {
                        error = "the server speaks a different version";
                        m_versionMismatch = true;
                        return false;
                    }
                    m_serverVersion = welcome.serverVersion;
                }
                catch (const Wire::WireError&)
                {
                    error = "the server sent something unreadable";
                    return false;
                }
                // The helper pushes its first Status after Welcome.
                if (!WaitFor(Type::Status, bytes, error))
                {
                    return false;
                }
                try
                {
                    Wire::Reader body(bytes);
                    ReadType(body);
                    m_latest = Status::Decode(body).status;
                }
                catch (const Wire::WireError&)
                {
                    error = "the server sent something unreadable";
                    return false;
                }
                return true;
            }

            bool ControlClient::RefreshStatus(ServerStatus& status, std::string& error)
            {
                if (!Send(GetStatus().Encode()))
                {
                    error = "could not talk to the server";
                    return false;
                }
                Wire::Bytes bytes;
                if (!WaitFor(Type::Status, bytes, error))
                {
                    return false;
                }
                try
                {
                    Wire::Reader body(bytes);
                    ReadType(body);
                    m_latest = Status::Decode(body).status;
                }
                catch (const Wire::WireError&)
                {
                    error = "the server sent something unreadable";
                    return false;
                }
                status = m_latest;
                return true;
            }

            ResultCode ControlClient::Request(const Wire::Bytes& message, Type requestType, std::string& text)
            {
                if (!Send(message))
                {
                    text = "could not talk to the server";
                    return ResultCode::Failed;
                }
                Wire::Bytes bytes;
                if (!WaitFor(Type::Result, bytes, text))
                {
                    return ResultCode::Failed;
                }
                try
                {
                    Wire::Reader body(bytes);
                    ReadType(body);
                    Result result = Result::Decode(body);
                    if (result.request != requestType)
                    {
                        text = "the server answered a different request";
                        return ResultCode::Failed;
                    }
                    text = result.text;
                    return result.code;
                }
                catch (const Wire::WireError&)
                {
                    text = "the server sent something unreadable";
                    return ResultCode::Failed;
                }
            }

            ResultCode ControlClient::DisconnectViewer(uint32_t viewerId, std::string& text)
            {
                Control::DisconnectViewer message;
                message.viewerId = viewerId;
                return Request(message.Encode(), Type::DisconnectViewer, text);
            }

            ResultCode ControlClient::SetPasswordHash(const std::string& passwordHash, std::string& text)
            {
                SetPassword message;
                message.passwordHash = passwordHash;
                return Request(message.Encode(), Type::SetPassword, text);
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
