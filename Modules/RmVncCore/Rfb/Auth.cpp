// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB security type handlers

#include <RmVncCore/Rfb/Auth.h>

#include <RmVncCore/Text.h>

#include <Crypto/Crypto.h>

#include <cstring>

namespace RmVnc
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

            Credentials RequestCredentials(const CredentialsProvider& provider, bool needUsername)
            {
                Credentials credentials;
                if (!provider || !provider(needUsername, credentials))
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
                Credentials credentials = RequestCredentials(provider, true);
                out.Send(ComputeArdResponse(generator, prime, serverPublicKey, credentials));
            }

            void AuthenticateVnc(InStream& in, ByteSink& out, const CredentialsProvider& provider)
            {
                Bytes challenge = in.ReadBytes(16);
                Credentials credentials = RequestCredentials(provider, false);
                out.Send(ComputeVncAuthResponse(credentials.password, challenge));
            }
        } // namespace

        const std::vector<uint8_t>& SupportedSecurityTypes()
        {
            // Apple's account login is preferred over the (DES, 8 character) VNC password when both are offered.
            static const std::vector<uint8_t> types = {SecurityType::None, SecurityType::AppleRemoteDesktop,
                                                       SecurityType::VncAuth};
            return types;
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

            // 64 bytes of username then 64 of password, each NUL terminated; the rest is random padding.
            uint8_t plain[128];
            if (!Crypto::RandomBytes(plain, sizeof(plain)))
            {
                throw RfbError("Could not generate random data");
            }
            std::string username = Utf8ToLatin1(credentials.username).substr(0, 63);
            std::string password = Utf8ToLatin1(credentials.password).substr(0, 63);
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

        void Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials)
        {
            switch (securityType)
            {
            case SecurityType::None:
                return;
            case SecurityType::VncAuth:
                AuthenticateVnc(in, out, credentials);
                return;
            case SecurityType::AppleRemoteDesktop:
                AuthenticateArd(in, out, credentials);
                return;
            default:
                throw RfbError("Unsupported security type " + std::to_string(securityType));
            }
        }
    } // namespace Rfb
} // namespace RmVnc
