// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB connection setup: version negotiation, security/authentication, ClientInit / ServerInit

#pragma once

#include <MarconiCore/Rfb/Messages.h>
#include <MarconiCore/Rfb/Stream.h>

#include <functional>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rfb
    {
        struct Credentials
        {
            std::string username;
            std::string password;
        };

        enum class CredentialsKind
        {
            Password,          // a VNC password
            Account,           // a user name and password (Apple Remote Desktop)
            PasswordOrAccount, // a Mac offering both: a user name selects the account login, none the VNC password
        };

        // Asked for credentials when the server requires them. Returns false if the user cancelled.
        using CredentialsProvider = std::function<bool(CredentialsKind kind, Credentials& inOutCredentials)>;

        struct ServerInit
        {
            int width = 0;
            int height = 0;
            PixelFormat pixelFormat;
            std::string name;
        };

        struct HandshakeResult
        {
            int serverMajor = 0;
            int serverMinor = 0;
            int minor = 0; // the 3.x version we speak: 3, 7 or 8
            uint8_t securityType = SecurityType::Invalid;
            ServerInit serverInit;
        };

        // Thrown when the server rejects the credentials, so the UI can offer to retry with different ones.
        class AuthenticationFailed : public RfbError
        {
        public:
            using RfbError::RfbError;
        };

        // Thrown when the user declines to enter credentials; the connection ends without an error message.
        class AuthenticationCancelled : public RfbError
        {
        public:
            using RfbError::RfbError;
        };

        // Parses "RFB xxx.yyy\n". Returns false if the banner isn't an RFB version string.
        bool ParseVersion(const std::string& banner, int& major, int& minor);

        // Picks the security type we'll use from those the server offers, or Invalid if none is supported.
        // A Mac with "VNC viewers may control screen with password" offers both its account login and the plain VNC
        // password; the VNC password wins unless `preferAccountLogin` is set. PerformHandshake decides that by
        // asking for credentials first: a user name means the account login.
        uint8_t ChooseSecurityType(const std::vector<uint8_t>& offered, bool preferAccountLogin = false);

        // Runs the whole handshake up to and including ServerInit. Throws RfbError / AuthenticationFailed.
        HandshakeResult PerformHandshake(InStream& in, ByteSink& out, bool shared, const CredentialsProvider& credentials);
    } // namespace Rfb
} // namespace Marconi
