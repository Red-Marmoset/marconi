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

        // Runs the security-type specific exchange (everything between choosing the type and SecurityResult).
        void Authenticate(uint8_t securityType, InStream& in, ByteSink& out, const CredentialsProvider& credentials);
    } // namespace Rfb
} // namespace RmVnc
