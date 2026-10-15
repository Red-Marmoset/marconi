// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RFB security type handlers

#include <RmVncCore/Rfb/Auth.h>

namespace RmVnc
{
    namespace Rfb
    {
        const std::vector<uint8_t>& SupportedSecurityTypes()
        {
            static const std::vector<uint8_t> types = {SecurityType::None};
            return types;
        }

        void Authenticate(uint8_t securityType, InStream&, ByteSink&, const CredentialsProvider&)
        {
            switch (securityType)
            {
            case SecurityType::None:
                return;
            default:
                throw RfbError("Unsupported security type " + std::to_string(securityType));
            }
        }
    } // namespace Rfb
} // namespace RmVnc
