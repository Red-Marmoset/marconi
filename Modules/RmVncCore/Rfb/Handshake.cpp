// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB connection setup: version negotiation, security/authentication, ClientInit / ServerInit

#include <RmVncCore/Rfb/Handshake.h>

#include <RmVncCore/Rfb/Auth.h>

#include <Log/Log.h>

#include <cstdio>

namespace RmVnc
{
    namespace Rfb
    {
        namespace
        {
            constexpr uint32_t kMaxReasonLength = 64 * 1024;
            constexpr uint32_t kMaxNameLength = 64 * 1024;

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

            uint8_t NegotiateSecurityType(InStream& in, ByteSink& out, int minor)
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

                uint8_t chosen = ChooseSecurityType(std::vector<uint8_t>(offered.begin(), offered.end()));
                if (chosen == SecurityType::Invalid)
                {
                    throw RfbError("The server requires an authentication method rmvnc doesn't support yet "
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

        uint8_t ChooseSecurityType(const std::vector<uint8_t>& offered)
        {
            // Most convenient first. Apple's account login comes before the VNC password, because macOS only
            // offers the VNC password when "VNC viewers may control screen with password" is switched on.
            for (uint8_t preferred : SupportedSecurityTypes())
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

        HandshakeResult PerformHandshake(InStream& in, ByteSink& out, bool shared, const CredentialsProvider& credentials)
        {
            HandshakeResult result;
            result.minor = NegotiateVersion(in, out, result);
            result.securityType = NegotiateSecurityType(in, out, result.minor);
            LOG_INFO("Rfb", "Using security type %d", result.securityType);

            Authenticate(result.securityType, in, out, credentials);

            // RFB 3.3 and 3.7 send no SecurityResult after "None".
            if (result.securityType != SecurityType::None || result.minor >= 8)
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
            LOG_INFO("Rfb", "Desktop '%s' %dx%d, server pixel format %d bpp depth %d", init.name.c_str(), init.width,
                     init.height, init.pixelFormat.bitsPerPixel, init.pixelFormat.depth);
            return result;
        }
    } // namespace Rfb
} // namespace RmVnc
