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
            Account,           // a user name and password (Apple Remote Desktop, VeNCrypt Plain)
            PasswordOrAccount, // a Mac offering both: a user name selects the account login, none the VNC password
            SshAccount,        // the account on the computer's SSH server (Remote Login), for a connection through SSH
        };

        // Asked for credentials when the server requires them. Returns false if the user cancelled.
        using CredentialsProvider = std::function<bool(CredentialsKind kind, Credentials& inOutCredentials)>;

        // Switches the connection to TLS when a VeNCrypt subtype calls for it: runs the TLS handshake and, from then
        // on, the stream being read and the sink being written carry the encrypted connection. `anonymous` means the
        // server presents no certificate. Throws RfbError if encryption can't be set up. Left empty in a build that
        // can't encrypt, so an encrypted security type is never chosen.
        using TlsStarter = std::function<void(bool anonymous)>;

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
            uint32_t veNCryptSubtype = 0; // with VeNCrypt: the VeNCrypt::* subtype in use
            bool encrypted = false;       // the connection is encrypted from here on
            bool tightFileTransfer = false; // Tight security: the server offers TightVNC file transfer
            ServerInit serverInit;
        };

        // Thrown when the server rejects the credentials, so the UI can offer to retry with different ones.
        class AuthenticationFailed : public RfbError
        {
        public:
            using RfbError::RfbError;
        };

        // Thrown when the user declines to enter credentials or to trust the server's certificate; the connection
        // ends without an error message.
        class AuthenticationCancelled : public RfbError
        {
        public:
            using RfbError::RfbError;
        };

        // Thrown when the server was asked for encryption but offers only kinds this computer can't make (anonymous
        // TLS without a certificate, say). Connecting again without asking for encryption may work.
        class EncryptionUnavailable : public RfbError
        {
        public:
            using RfbError::RfbError;
        };

        // Parses "RFB xxx.yyy\n". Returns false if the banner isn't an RFB version string.
        bool ParseVersion(const std::string& banner, int& major, int& minor);

        // Picks the security type we'll use from those the server offers, or Invalid if none is supported.
        // An encrypted connection (VeNCrypt) comes first when `tlsAvailable`. A Mac with "VNC viewers may control
        // screen with password" offers both its account login and the plain VNC password; the VNC password wins
        // unless `preferAccountLogin` is set. PerformHandshake decides that by asking for credentials first: a user
        // name means the account login. With `allowTight`, TightVNC's own security type (16) comes next: it logs in
        // the same way but lets the server offer file transfer.
        uint8_t ChooseSecurityType(const std::vector<uint8_t>& offered, bool preferAccountLogin = false,
                                   bool tlsAvailable = false, bool allowTight = false);

        // Runs the whole handshake up to and including ServerInit (and, with Tight security, the server's feature
        // lists). Throws RfbError / AuthenticationFailed.
        // `anonymousTls` says whether `startTls` can make a connection without a server certificate.
        HandshakeResult PerformHandshake(InStream& in, ByteSink& out, bool shared,
                                         const CredentialsProvider& credentials, const TlsStarter& startTls = nullptr,
                                         bool allowTight = false, bool* outChoseTight = nullptr,
                                         bool anonymousTls = true);
    } // namespace Rfb
} // namespace Marconi
