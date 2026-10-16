// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB connection setup: version negotiation, security/authentication, ClientInit / ServerInit

#include <MarconiCore/Rfb/Handshake.h>

#include <MarconiCore/Rfb/Auth.h>
#include <MarconiCore/Rfb/TightFileTransfer.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstdio>
#include <optional>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kMaxReasonLength = 64 * 1024;
            constexpr uint32_t kMaxNameLength = 64 * 1024;
            constexpr uint32_t kMaxCapabilities = 1024;

            // TightVNC lists features as 16 bytes each: a code, a 4-byte vendor and an 8-byte signature.
            struct Capability
            {
                uint32_t code = 0;
                std::string vendor;
                std::string signature;
            };

            std::vector<Capability> ReadCapabilities(InStream& in, uint32_t count)
            {
                if (count > kMaxCapabilities)
                {
                    throw RfbError("The server sent an invalid capability list");
                }
                std::vector<Capability> capabilities(count);
                for (Capability& capability : capabilities)
                {
                    capability.code = in.U32();
                    capability.vendor = in.ReadString(4);
                    capability.signature = in.ReadString(8);
                }
                return capabilities;
            }

            // Tight security (16): no tunnel, then pick the login method. Returns the plain security type that
            // then runs (None or VncAuth).
            uint8_t NegotiateTight(InStream& in, ByteSink& out)
            {
                constexpr uint32_t kNoTunnel = 0;
                constexpr uint32_t kAuthNone = 1;
                constexpr uint32_t kAuthVnc = 2;
                uint32_t tunnels = in.U32();
                if (tunnels > 0)
                {
                    ReadCapabilities(in, tunnels);
                    OutBuffer choice;
                    choice.U32(kNoTunnel);
                    out.Send(choice.Data());
                }
                uint32_t methods = in.U32();
                if (methods == 0)
                {
                    return SecurityType::None;
                }
                std::vector<Capability> offered = ReadCapabilities(in, methods);
                for (uint32_t wanted : {kAuthVnc, kAuthNone})
                {
                    for (const Capability& method : offered)
                    {
                        if (method.code == wanted)
                        {
                            OutBuffer choice;
                            choice.U32(wanted);
                            out.Send(choice.Data());
                            return wanted == kAuthVnc ? SecurityType::VncAuth : SecurityType::None;
                        }
                    }
                }
                throw RfbError("The server requires a TightVNC login method Marconi doesn't support");
            }

            std::string ReadReason(InStream& in)
            {
                uint32_t length = in.U32();
                if (length > kMaxReasonLength)
                {
                    throw RfbError("The server sent an invalid error message");
                }
                return in.ReadString(length);
            }

            int NegotiateVersion(InStream& in, ByteSink& out, HandshakeResult& result)
            {
                std::string banner = in.ReadString(12);
                if (!ParseVersion(banner, result.serverMajor, result.serverMinor))
                {
                    throw RfbError("This doesn't look like a VNC server (no RFB version banner)");
                }
                if (result.serverMajor != 3 || result.serverMinor < 3)
                {
                    throw RfbError("Unsupported RFB protocol version " + std::to_string(result.serverMajor) + "." +
                                   std::to_string(result.serverMinor));
                }

                // 3.4/3.6 (UltraVNC) and anything below 3.7 speak 3.3. 3.8 and later, including Apple's 3.889, speak 3.8.
                int minor = result.serverMinor >= 8 ? 8 : (result.serverMinor == 7 ? 7 : 3);
                char reply[13];
                std::snprintf(reply, sizeof(reply), "RFB 003.%03d\n", minor);
                out.Send(Bytes(reply, reply + 12));
                LOG_INFO("Rfb", "Server speaks RFB %d.%d, using 3.%d", result.serverMajor, result.serverMinor, minor);
                return minor;
            }

            // Returns the chosen type. When the choice depends on the credentials, they're requested here and
            // `outCredentials` is set, so authentication must not ask again.
            uint8_t NegotiateSecurityType(InStream& in, ByteSink& out, int minor, const CredentialsProvider& provider,
                                          bool tlsAvailable, bool allowTight,
                                          std::optional<Credentials>& outCredentials)
            {
                if (minor == 3)
                {
                    // 3.3: the server decides.
                    uint32_t type = in.U32();
                    if (type == SecurityType::Invalid)
                    {
                        throw RfbError("The server refused the connection: " + ReadReason(in));
                    }
                    if (type > 255)
                    {
                        throw RfbError("The server chose an unknown security type " + std::to_string(type));
                    }
                    return static_cast<uint8_t>(type);
                }

                uint8_t count = in.U8();
                if (count == 0)
                {
                    throw RfbError("The server refused the connection: " + ReadReason(in));
                }
                Bytes offered = in.ReadBytes(count);
                std::string offeredText;
                for (uint8_t type : offered)
                {
                    offeredText += (offeredText.empty() ? "" : ", ") + std::to_string(type);
                }
                LOG_INFO("Rfb", "Server offers security types [%s]", offeredText.c_str());

                std::vector<uint8_t> types(offered.begin(), offered.end());
                auto isOffered = [&types](uint8_t type) {
                    return std::find(types.begin(), types.end(), type) != types.end();
                };
                bool preferAccountLogin = false;
                if (isOffered(SecurityType::AppleRemoteDesktop) && isOffered(SecurityType::VncAuth) &&
                    !isOffered(SecurityType::None))
                {
                    Credentials credentials;
                    if (!provider || !provider(CredentialsKind::PasswordOrAccount, credentials))
                    {
                        throw AuthenticationCancelled("Authentication cancelled");
                    }
                    preferAccountLogin = !credentials.username.empty();
                    outCredentials = credentials;
                }
                uint8_t chosen = ChooseSecurityType(types, preferAccountLogin, tlsAvailable, allowTight);
                if (chosen == SecurityType::Invalid)
                {
                    if (isOffered(SecurityType::VeNCrypt))
                    {
                        throw RfbError("The server requires an encrypted connection, which this build of Marconi "
                                       "can't make yet (offered security types: " + offeredText + ")");
                    }
                    throw RfbError("The server requires an authentication method Marconi doesn't support yet "
                                   "(offered security types: " + offeredText + ")");
                }
                OutBuffer choice;
                choice.U8(chosen);
                out.Send(choice.Data());
                return chosen;
            }

            void ReadSecurityResult(InStream& in, int minor)
            {
                uint32_t status = in.U32();
                if (status == 0)
                {
                    return;
                }
                std::string reason = minor >= 8 ? ReadReason(in) : std::string();
                if (status == 2)
                {
                    throw RfbError("Too many authentication failures. " + reason);
                }
                throw AuthenticationFailed(reason.empty() ? std::string("Authentication failed") : reason);
            }
        } // namespace

        bool ParseVersion(const std::string& banner, int& major, int& minor)
        {
            if (banner.size() != 12 || banner.compare(0, 4, "RFB ") != 0 || banner[7] != '.' || banner[11] != '\n')
            {
                return false;
            }
            for (int i : {4, 5, 6, 8, 9, 10})
            {
                if (banner[i] < '0' || banner[i] > '9')
                {
                    return false;
                }
            }
            major = std::stoi(banner.substr(4, 3));
            minor = std::stoi(banner.substr(8, 3));
            return true;
        }

        uint8_t ChooseSecurityType(const std::vector<uint8_t>& offered, bool preferAccountLogin, bool tlsAvailable,
                                   bool allowTight)
        {
            auto isOffered = [&offered](uint8_t type) {
                return std::find(offered.begin(), offered.end(), type) != offered.end();
            };
            if (tlsAvailable && isOffered(SecurityType::VeNCrypt))
            {
                return SecurityType::VeNCrypt;
            }
            if (allowTight && isOffered(SecurityType::Tight))
            {
                return SecurityType::Tight;
            }
            if (preferAccountLogin && isOffered(SecurityType::AppleRemoteDesktop))
            {
                return SecurityType::AppleRemoteDesktop;
            }
            for (uint8_t preferred : SupportedSecurityTypes(tlsAvailable))
            {
                for (uint8_t type : offered)
                {
                    if (type == preferred)
                    {
                        return type;
                    }
                }
            }
            return SecurityType::Invalid;
        }

        HandshakeResult PerformHandshake(InStream& in, ByteSink& out, bool shared,
                                         const CredentialsProvider& credentials, const TlsStarter& startTls,
                                         bool allowTight)
        {
            HandshakeResult result;
            result.minor = NegotiateVersion(in, out, result);
            std::optional<Credentials> given;
            result.securityType =
                NegotiateSecurityType(in, out, result.minor, credentials, startTls != nullptr, allowTight, given);
            LOG_INFO("Rfb", "Using security type %d", result.securityType);
            // Tight wraps a plain login: after its own negotiation, that login runs as usual.
            uint8_t login = result.securityType == SecurityType::Tight ? NegotiateTight(in, out) : result.securityType;

            if (given)
            {
                Credentials answer = *given;
                result.veNCryptSubtype = Authenticate(
                    login, in, out,
                    [answer](CredentialsKind, Credentials& answerOut) {
                        answerOut = answer;
                        return true;
                    },
                    startTls);
            }
            else
            {
                result.veNCryptSubtype = Authenticate(login, in, out, credentials, startTls);
            }
            result.encrypted =
                result.securityType == SecurityType::VeNCrypt && result.veNCryptSubtype != VeNCrypt::Plain;

            // RFB 3.3 and 3.7 send no SecurityResult after "None".
            if (login != SecurityType::None || result.minor >= 8)
            {
                ReadSecurityResult(in, result.minor);
            }

            OutBuffer clientInit;
            clientInit.U8(shared ? 1 : 0);
            out.Send(clientInit.Data());

            ServerInit& init = result.serverInit;
            init.width = in.U16();
            init.height = in.U16();
            init.pixelFormat = PixelFormat::Read(in);
            uint32_t nameLength = in.U32();
            if (nameLength > kMaxNameLength)
            {
                throw RfbError("The server sent an invalid desktop name");
            }
            init.name = in.ReadString(nameLength);
            if (result.securityType == SecurityType::Tight)
            {
                // The server's message types, the client message types it accepts, and its encodings.
                uint16_t serverMessages = in.U16();
                uint16_t clientMessages = in.U16();
                uint16_t encodings = in.U16();
                in.Skip(2);
                ReadCapabilities(in, serverMessages);
                for (const Capability& capability : ReadCapabilities(in, clientMessages))
                {
                    if (capability.code == FileTransfer::FileListRequest && capability.vendor == "TGHT")
                    {
                        result.tightFileTransfer = true;
                    }
                }
                ReadCapabilities(in, encodings);
                LOG_INFO("Rfb", "TightVNC extensions: file transfer %s", result.tightFileTransfer ? "offered" : "not offered");
            }
            LOG_INFO("Rfb", "Desktop '%s' %dx%d, server pixel format %d bpp depth %d", init.name.c_str(), init.width,
                     init.height, init.pixelFormat.bitsPerPixel, init.pixelFormat.depth);
            return result;
        }
    } // namespace Rfb
} // namespace Marconi
