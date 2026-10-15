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

            void AuthenticateVnc(InStream& in, ByteSink& out, const CredentialsProvider& provider)
            {
                Bytes challenge = in.ReadBytes(16);
                Credentials credentials = RequestCredentials(provider, false);
                out.Send(ComputeVncAuthResponse(credentials.password, challenge));
            }
        } // namespace

        const std::vector<uint8_t>& SupportedSecurityTypes()
        {
            static const std::vector<uint8_t> types = {SecurityType::None, SecurityType::VncAuth};
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

        void Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials)
        {
            switch (securityType)
            {
            case SecurityType::None:
                return;
            case SecurityType::VncAuth:
                AuthenticateVnc(in, out, credentials);
                return;
            default:
                throw RfbError("Unsupported security type " + std::to_string(securityType));
            }
        }
    } // namespace Rfb
} // namespace RmVnc
