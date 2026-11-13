// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the control pipe's messages into bytes and back

#include "ControlProtocol.h"

#include <MarconiCore/Wire/WireMessages.h> // Wire::Frame

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            namespace
            {
                template <class WriteBody>
                Wire::Bytes Build(Type type, WriteBody writeBody)
                {
                    Wire::Writer body;
                    body.U8(static_cast<uint8_t>(type));
                    writeBody(body);
                    return Wire::Frame(body.Data());
                }

                // A viewer is at least an id (4), a name length (1) and the seconds (4) = 9 bytes.
                constexpr size_t kMinViewerBytes = 9;
            } // namespace

            bool ServerStatus::operator==(const ServerStatus& other) const
            {
                if (serverVersion != other.serverVersion || port != other.port || passwordSet != other.passwordSet ||
                    lockOnDisconnect != other.lockOnDisconnect || consoleSession != other.consoleSession ||
                    desktop != other.desktop || fingerprint != other.fingerprint || viewers.size() != other.viewers.size())
                {
                    return false;
                }
                for (size_t i = 0; i < viewers.size(); ++i)
                {
                    if (viewers[i].id != other.viewers[i].id || viewers[i].name != other.viewers[i].name ||
                        viewers[i].connectedSeconds != other.viewers[i].connectedSeconds)
                    {
                        return false;
                    }
                }
                return true;
            }

            Type ReadType(Wire::Reader& reader)
            {
                return static_cast<Type>(reader.U8());
            }

            Wire::Bytes Hello::Encode() const
            {
                return Build(Type::Hello, [&](Wire::Writer& w) { w.U16(version).String(clientName); });
            }

            Hello Hello::Decode(Wire::Reader& r)
            {
                Hello message;
                message.version = r.U16();
                message.clientName = r.String();
                return message;
            }

            Wire::Bytes Welcome::Encode() const
            {
                return Build(Type::Welcome, [&](Wire::Writer& w) { w.U16(version).String(serverVersion); });
            }

            Welcome Welcome::Decode(Wire::Reader& r)
            {
                Welcome message;
                message.version = r.U16();
                message.serverVersion = r.String();
                return message;
            }

            Wire::Bytes GetStatus::Encode() const
            {
                return Build(Type::GetStatus, [](Wire::Writer&) {});
            }

            GetStatus GetStatus::Decode(Wire::Reader&)
            {
                return GetStatus();
            }

            Wire::Bytes Status::Encode() const
            {
                return Build(Type::Status, [&](Wire::Writer& w) {
                    w.String(status.serverVersion)
                        .U16(status.port)
                        .U8(static_cast<uint8_t>((status.passwordSet ? 1 : 0) | (status.lockOnDisconnect ? 2 : 0)))
                        .U32(status.consoleSession)
                        .String(status.desktop)
                        .Varint(status.viewers.size());
                    for (const ViewerInfo& viewer : status.viewers)
                    {
                        w.U32(viewer.id).String(viewer.name).U32(viewer.connectedSeconds);
                    }
                    w.String(status.fingerprint);
                });
            }

            Status Status::Decode(Wire::Reader& r)
            {
                Status message;
                ServerStatus& s = message.status;
                s.serverVersion = r.String();
                s.port = r.U16();
                uint8_t flags = r.U8();
                s.passwordSet = (flags & 1) != 0;
                s.lockOnDisconnect = (flags & 2) != 0;
                s.consoleSession = r.U32();
                s.desktop = r.String();
                uint64_t count = r.Varint();
                // Refuse a count the rest of the message can't hold before reserving for it.
                if (count > r.Remaining() / kMinViewerBytes)
                {
                    throw Wire::WireError("more viewers than the message can hold");
                }
                s.viewers.reserve(static_cast<size_t>(count));
                for (uint64_t i = 0; i < count; ++i)
                {
                    ViewerInfo viewer;
                    viewer.id = r.U32();
                    viewer.name = r.String();
                    viewer.connectedSeconds = r.U32();
                    s.viewers.push_back(std::move(viewer));
                }
                if (!r.AtEnd())
                {
                    s.fingerprint = r.String(); // added after version 1 first shipped; older helpers stop before it
                }
                return message;
            }

            Wire::Bytes DisconnectViewer::Encode() const
            {
                return Build(Type::DisconnectViewer, [&](Wire::Writer& w) { w.U32(viewerId); });
            }

            DisconnectViewer DisconnectViewer::Decode(Wire::Reader& r)
            {
                DisconnectViewer message;
                message.viewerId = r.U32();
                return message;
            }

            Wire::Bytes SetPassword::Encode() const
            {
                return Build(Type::SetPassword, [&](Wire::Writer& w) { w.String(passwordHash); });
            }

            SetPassword SetPassword::Decode(Wire::Reader& r)
            {
                SetPassword message;
                message.passwordHash = r.String();
                return message;
            }

            Wire::Bytes Result::Encode() const
            {
                return Build(Type::Result, [&](Wire::Writer& w) {
                    w.U8(static_cast<uint8_t>(request)).U8(static_cast<uint8_t>(code)).String(text);
                });
            }

            Result Result::Decode(Wire::Reader& r)
            {
                Result message;
                message.request = static_cast<Type>(r.U8());
                message.code = static_cast<ResultCode>(r.U8());
                message.text = r.String();
                return message;
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
