// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The messages the tray icon and the session helper send each other over the local control pipe: status, who is
// connected, Disconnect viewer, Set password. They use the Marconi protocol's framing (a varint length, then a type
// byte and the body) with their own type numbers; the two never share a connection.

#pragma once

#include <MarconiCore/Wire/WireStream.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            // Bumped when a message changes in a way an older tray icon or helper can't read.
            constexpr uint16_t kVersion = 1;

            // The Windows pipe name; the tray icon connects to it, the helper creates it.
            constexpr const char* kPipeName = "\\\\.\\pipe\\MarconiServerControl";

            enum class Type : uint8_t
            {
                // helper -> tray
                Welcome = 1, // answer to Hello: version, the server's version
                Status = 2,  // the server's current state; sent after Hello, when asked and whenever it changes
                Result = 3,  // the outcome of a request that has no other answer

                // tray -> helper
                Hello = 16,            // first message: version, a name for the log
                GetStatus = 17,        // ask for a Status now
                DisconnectViewer = 18, // end one viewer's connection (or all)
                SetPassword = 19,      // store a new access password hash (administrators only)
            };

            enum class ResultCode : uint8_t
            {
                Ok = 0,
                NotAdministrator = 1, // the request needs an administrator and the tray icon's user isn't one
                NoSuchViewer = 2,     // Disconnect viewer named a viewer that isn't connected
                Failed = 3,           // the helper tried and couldn't (the text says why)
                BadRequest = 4        // malformed or not allowed in this state (for example before Hello)
            };

            struct ViewerInfo
            {
                uint32_t id = 0;              // names the viewer in DisconnectViewer; never 0
                std::string name;             // what the viewer called itself in its Hello
                uint32_t connectedSeconds = 0; // how long it has been connected
            };

            // What the helper tells the tray icon about the server.
            struct ServerStatus
            {
                std::string serverVersion;
                uint16_t port = 0;
                bool passwordSet = false;
                bool lockOnDisconnect = false;
                uint32_t consoleSession = 0; // the Windows session the helper serves
                std::string desktop;         // the desktop it is looking at ("Default", "Winlogon" for UAC and lock)
                std::vector<ViewerInfo> viewers;
                // The certificate fingerprint viewers are asked to trust (lowercase hex), so the tray icon can show it.
                // Sent after the viewers; an older helper leaves it out and it reads as empty.
                std::string fingerprint;

                bool operator==(const ServerStatus& other) const;
                bool operator!=(const ServerStatus& other) const { return !(*this == other); }
            };

            // -- The messages: Encode returns framed bytes, Decode reads after the type byte --------------------

            struct Hello
            {
                uint16_t version = kVersion;
                std::string clientName; // "tray icon" and the user's name, for the helper's log
                Wire::Bytes Encode() const;
                static Hello Decode(Wire::Reader& reader);
            };

            struct Welcome
            {
                uint16_t version = kVersion;
                std::string serverVersion;
                Wire::Bytes Encode() const;
                static Welcome Decode(Wire::Reader& reader);
            };

            struct GetStatus
            {
                Wire::Bytes Encode() const;
                static GetStatus Decode(Wire::Reader& reader);
            };

            struct Status
            {
                ServerStatus status;
                Wire::Bytes Encode() const;
                static Status Decode(Wire::Reader& reader);
            };

            struct DisconnectViewer
            {
                uint32_t viewerId = 0; // 0 = every viewer
                Wire::Bytes Encode() const;
                static DisconnectViewer Decode(Wire::Reader& reader);
            };

            struct SetPassword
            {
                // The stored hash (from HashPassword), never the plain password: the tray icon hashes what the
                // person typed, so the password doesn't cross the pipe. Empty clears the password.
                std::string passwordHash;
                Wire::Bytes Encode() const;
                static SetPassword Decode(Wire::Reader& reader);
            };

            struct Result
            {
                Type request = Type::GetStatus; // which request this answers
                ResultCode code = ResultCode::Ok;
                std::string text; // a plain-words reason for the user when code isn't Ok
                Wire::Bytes Encode() const;
                static Result Decode(Wire::Reader& reader);
            };

            Type ReadType(Wire::Reader& reader);
        } // namespace Control
    } // namespace Server
} // namespace Marconi
