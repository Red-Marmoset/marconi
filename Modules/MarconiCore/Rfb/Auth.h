// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB security type handlers

#pragma once

#include <MarconiCore/Rfb/Handshake.h>

#include <vector>

namespace Marconi
{
    namespace Rfb
    {
        // Security types Marconi implements, most preferred first. An encrypted connection (VeNCrypt) leads when
        // this build can make one.
        std::vector<uint8_t> SupportedSecurityTypes(bool tlsAvailable);

        // Picks the VeNCrypt subtype to use from those the server offers, or 0 if none is acceptable: a server
        // certificate beats anonymous encryption, and within each, no login beats the VNC password beats a user
        // name and password. The unencrypted Plain subtype is never chosen.
        uint32_t ChooseVeNCryptSubtype(const std::vector<uint32_t>& offered);

        // VNC Authentication (type 2): DES-encrypts the 16 byte challenge with the password as key.
        Bytes ComputeVncAuthResponse(const std::string& passwordUtf8, const Bytes& challenge);

        // Apple Remote Desktop authentication (type 30) client side, split out for tests: given the server's DH
        // parameters and public key, returns what the client sends (128 byte AES-encrypted credentials followed
        // by the client's public key).
        Bytes ComputeArdResponse(const Bytes& generator, const Bytes& prime, const Bytes& serverPublicKey,
                                 const Credentials& credentials);

        // Runs the security-type specific exchange (everything between choosing the type and SecurityResult).
        // Returns the VeNCrypt subtype in use, or 0 for every other type.
        uint32_t Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials,
                              const TlsStarter& startTls);
    } // namespace Rfb
} // namespace Marconi
