// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB security type handlers

#include <MarconiCore/Rfb/Auth.h>

#include <MarconiCore/Text.h>

#include <Crypto/Crypto.h>

#include <Log/Log.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            uint8_t ReverseBits(uint8_t value)
            {
                uint8_t result = 0;
                for (int bit = 0; bit < 8; ++bit)
                {
                    result = static_cast<uint8_t>((result << 1) | ((value >> bit) & 1));
                }
                return result;
            }

            // At most `maxBytes` bytes, without splitting a multi-byte UTF-8 sequence.
            std::string TruncateUtf8(const std::string& text, size_t maxBytes)
            {
                if (text.size() <= maxBytes)
                {
                    return text;
                }
                size_t length = maxBytes;
                while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80)
                {
                    --length;
                }
                return text.substr(0, length);
            }

            Credentials RequestCredentials(const CredentialsProvider& provider, CredentialsKind kind)
            {
                Credentials credentials;
                if (!provider || !provider(kind, credentials))
                {
                    throw AuthenticationCancelled("Authentication cancelled");
                }
                return credentials;
            }

            void AuthenticateArd(InStream& in, ByteSink& out, const CredentialsProvider& provider)
            {
                Bytes generator = in.ReadBytes(2);
                uint16_t keyLength = in.U16();
                if (keyLength < 16 || keyLength > 1024)
                {
                    throw RfbError("The server sent an invalid Apple authentication key length");
                }
                Bytes prime = in.ReadBytes(keyLength);
                Bytes serverPublicKey = in.ReadBytes(keyLength);
                Credentials credentials = RequestCredentials(provider, CredentialsKind::Account);
                out.Send(ComputeArdResponse(generator, prime, serverPublicKey, credentials));
            }

            void AuthenticateVnc(InStream& in, ByteSink& out, const CredentialsProvider& provider)
            {
                Bytes challenge = in.ReadBytes(16);
                Credentials credentials = RequestCredentials(provider, CredentialsKind::Password);
                out.Send(ComputeVncAuthResponse(credentials.password, challenge));
            }

            // VeNCrypt's Plain login: the user name and password as they are, which is safe only because the
            // connection is already encrypted when this runs.
            void AuthenticatePlain(ByteSink& out, const CredentialsProvider& provider)
            {
                Credentials credentials = RequestCredentials(provider, CredentialsKind::Account);
                OutBuffer message;
                message.U32(static_cast<uint32_t>(credentials.username.size()))
                    .U32(static_cast<uint32_t>(credentials.password.size()))
                    .Append(credentials.username)
                    .Append(credentials.password);
                out.Send(message.Data());
            }

            std::string ListSubtypes(const std::vector<uint32_t>& subtypes)
            {
                std::string text;
                for (uint32_t subtype : subtypes)
                {
                    text += (text.empty() ? "" : ", ") + std::to_string(subtype);
                }
                return text;
            }

            // VeNCrypt: agree on version 0.2, pick an encrypted subtype, start TLS, then run the subtype's login.
            uint32_t AuthenticateVeNCrypt(InStream& in, ByteSink& out, const CredentialsProvider& provider,
                                          const TlsStarter& startTls)
            {
                if (!startTls)
                {
                    throw RfbError("This build of Marconi can't make encrypted connections yet");
                }
                uint8_t major = in.U8();
                uint8_t minor = in.U8();
                LOG_INFO("Rfb", "Server speaks VeNCrypt %d.%d", major, minor);
                if (major != 0 || minor < 1)
                {
                    throw RfbError("The server uses a version of VeNCrypt Marconi doesn't support (" +
                                   std::to_string(major) + "." + std::to_string(minor) + ")");
                }
                OutBuffer version;
                version.U8(0).U8(2);
                out.Send(version.Data());
                if (in.U8() != 0)
                {
                    throw RfbError("The server rejected VeNCrypt version 0.2");
                }

                uint8_t count = in.U8();
                std::vector<uint32_t> offered;
                for (uint8_t i = 0; i < count; ++i)
                {
                    offered.push_back(in.U32());
                }
                LOG_INFO("Rfb", "Server offers VeNCrypt subtypes [%s]", ListSubtypes(offered).c_str());
                uint32_t chosen = ChooseVeNCryptSubtype(offered);
                if (chosen == 0)
                {
                    throw RfbError("The server offers no encrypted login method Marconi supports "
                                   "(VeNCrypt subtypes: " + ListSubtypes(offered) + ")");
                }
                OutBuffer choice;
                choice.U32(chosen);
                out.Send(choice.Data());
                if (in.U8() != 1)
                {
                    throw RfbError("The server couldn't start encryption");
                }

                bool anonymous = chosen == VeNCrypt::TlsNone || chosen == VeNCrypt::TlsVnc || chosen == VeNCrypt::TlsPlain;
                startTls(anonymous);
                LOG_INFO("Rfb", "Connection encrypted; logging in with VeNCrypt subtype %u", chosen);
                switch (chosen)
                {
                case VeNCrypt::TlsVnc:
                case VeNCrypt::X509Vnc:
                    AuthenticateVnc(in, out, provider);
                    break;
                case VeNCrypt::TlsPlain:
                case VeNCrypt::X509Plain:
                    AuthenticatePlain(out, provider);
                    break;
                default:
                    break; // TlsNone / X509None: nothing more to send
                }
                return chosen;
            }
        } // namespace

        std::vector<uint8_t> SupportedSecurityTypes(bool tlsAvailable)
        {
            // Encryption first. Then the VNC password before Apple's account login: when a Mac offers both, the
            // password is what users of other viewers expect to type (ChooseSecurityType can still prefer the
            // account login).
            std::vector<uint8_t> types;
            if (tlsAvailable)
            {
                types.push_back(SecurityType::VeNCrypt);
            }
            types.insert(types.end(), {SecurityType::None, SecurityType::VncAuth, SecurityType::AppleRemoteDesktop});
            return types;
        }

        uint32_t ChooseVeNCryptSubtype(const std::vector<uint32_t>& offered)
        {
            static const uint32_t kPreferred[] = {VeNCrypt::X509None, VeNCrypt::X509Vnc, VeNCrypt::X509Plain,
                                                  VeNCrypt::TlsNone,  VeNCrypt::TlsVnc,  VeNCrypt::TlsPlain};
            for (uint32_t candidate : kPreferred)
            {
                if (std::find(offered.begin(), offered.end(), candidate) != offered.end())
                {
                    return candidate;
                }
            }
            return 0;
        }

        Bytes ComputeVncAuthResponse(const std::string& passwordUtf8, const Bytes& challenge)
        {
            if (challenge.size() != 16)
            {
                throw RfbError("Invalid VNC authentication challenge");
            }
            // The key is the first 8 password bytes, zero padded, with each byte's bits mirrored: a quirk of the
            // original VNC implementation that every server expects.
            std::string password = Utf8ToLatin1(passwordUtf8);
            uint8_t key[8] = {};
            for (size_t i = 0; i < 8 && i < password.size(); ++i)
            {
                key[i] = ReverseBits(static_cast<uint8_t>(password[i]));
            }
            Bytes response(16);
            bool ok = Crypto::DesEcbEncrypt(key, challenge.data(), response.data(), response.size());
            std::memset(key, 0, sizeof(key));
            if (!ok)
            {
                throw RfbError("DES encryption failed");
            }
            return response;
        }

        Bytes ComputeArdResponse(const Bytes& generator, const Bytes& prime, const Bytes& serverPublicKey,
                                 const Credentials& credentials)
        {
            Crypto::DhKeyPair keyPair;
            Bytes secret;
            if (!Crypto::DhGenerateKeyPair(prime, generator, keyPair) ||
                !Crypto::DhComputeSharedSecret(prime, keyPair, serverPublicKey, secret))
            {
                throw RfbError("Apple authentication key exchange failed");
            }
            uint8_t key[16];
            Crypto::Md5(secret.data(), secret.size(), key);

            // 64 bytes of username then 64 of password, UTF-8 and NUL terminated; the rest is random padding.
            uint8_t plain[128];
            if (!Crypto::RandomBytes(plain, sizeof(plain)))
            {
                throw RfbError("Could not generate random data");
            }
            std::string username = TruncateUtf8(credentials.username, 63);
            std::string password = TruncateUtf8(credentials.password, 63);
            std::memcpy(plain, username.c_str(), username.size() + 1);
            std::memcpy(plain + 64, password.c_str(), password.size() + 1);

            Bytes response(128);
            bool ok = Crypto::Aes128EcbEncrypt(key, plain, response.data(), sizeof(plain));
            std::memset(plain, 0, sizeof(plain));
            std::memset(key, 0, sizeof(key));
            if (!ok)
            {
                throw RfbError("AES encryption failed");
            }
            response.insert(response.end(), keyPair.publicKey.begin(), keyPair.publicKey.end());
            return response;
        }

        uint32_t Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials,
                              const TlsStarter& startTls)
        {
            switch (securityType)
            {
            case SecurityType::None:
                return 0;
            case SecurityType::VncAuth:
                AuthenticateVnc(in, out, credentials);
                return 0;
            case SecurityType::AppleRemoteDesktop:
                AuthenticateArd(in, out, credentials);
                return 0;
            case SecurityType::VeNCrypt:
                return AuthenticateVeNCrypt(in, out, credentials, startTls);
            default:
                throw RfbError("Unsupported security type " + std::to_string(securityType));
            }
        }
    } // namespace Rfb
} // namespace Marconi
