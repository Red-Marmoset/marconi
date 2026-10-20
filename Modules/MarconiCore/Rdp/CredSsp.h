// Copyright (c) 2026 Red Marmoset Studios Ltd.
// CredSSP (MS-CSSP), the protocol that carries the NTLM login over the already-established TLS channel for RDP's
// Network Level Authentication. This file is the message layer: the ASN.1 DER encoding of TSRequest and the
// credentials, which the exchange (CredSspExchange, later) fills in and reads back. See Documentation/RDP.md.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // TSRequest (MS-CSSP 2.2.1): the envelope exchanged over TLS. Each optional field is empty when absent;
        // `hasErrorCode` tells an all-zero error code apart from no error code at all.
        struct TsRequest
        {
            uint32_t version = 6;
            std::vector<Bytes> negoTokens; // the NTLM messages, one per round
            Bytes authInfo;                // the encrypted TSCredentials, on the last message
            Bytes pubKeyAuth;              // the encrypted public-key proof
            bool hasErrorCode = false;
            uint32_t errorCode = 0;
            Bytes clientNonce;             // 32 bytes, for version 5 and up's public-key hash
        };

        Bytes EncodeTsRequest(const TsRequest& request);
        // False if the bytes aren't a well-formed TSRequest.
        bool ParseTsRequest(const Bytes& der, TsRequest& out);

        // TSCredentials wrapping a TSPasswordCreds (MS-CSSP 2.2.1.2): domain, user and password, each UTF-16LE.
        // This is what gets encrypted into TSRequest::authInfo once the login has succeeded.
        Bytes EncodeTsCredentials(const std::string& domain, const std::string& user, const std::string& password);
    } // namespace Rdp
} // namespace Marconi
