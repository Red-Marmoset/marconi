// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB security type handlers

#pragma once

#include <RmVncCore/Rfb/Handshake.h>

#include <vector>

namespace RmVnc
{
    namespace Rfb
    {
        // Security types rmvnc implements, most preferred first.
        const std::vector<uint8_t>& SupportedSecurityTypes();

        // VNC Authentication (type 2): DES-encrypts the 16 byte challenge with the password as key.
        Bytes ComputeVncAuthResponse(const std::string& passwordUtf8, const Bytes& challenge);

        // Apple Remote Desktop authentication (type 30) client side, split out for tests: given the server's DH
        // parameters and public key, returns what the client sends (128 byte AES-encrypted credentials followed
        // by the client's public key).
        Bytes ComputeArdResponse(const Bytes& generator, const Bytes& prime, const Bytes& serverPublicKey,
                                 const Credentials& credentials);

        // Runs the security-type specific exchange (everything between choosing the type and SecurityResult).
        void Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials);
    } // namespace Rfb
} // namespace RmVnc
