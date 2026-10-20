// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the CredSSP message layer: the ASN.1 DER of TSRequest (MS-CSSP 2.2.1) and the credentials, by
// round-tripping and by checking the DER structure of hand-made cases.

#include "TestSupport.h"

#include <MarconiCore/Rdp/CredSsp.h>

#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    void RunRdpCredSspTests()
    {
        RunTest("A TSRequest with just a version and one token round-trips", []() {
            Rdp::TsRequest request;
            request.version = 6;
            request.negoTokens.push_back({'N', 'T', 'L', 'M', 0x01, 0x02});
            Rdp::Bytes der = Rdp::EncodeTsRequest(request);
            CHECK(der[0] == 0x30); // SEQUENCE

            Rdp::TsRequest parsed;
            CHECK(Rdp::ParseTsRequest(der, parsed));
            CHECK(parsed.version == 6);
            CHECK(parsed.negoTokens.size() == 1);
            CHECK(parsed.negoTokens[0] == request.negoTokens[0]);
            CHECK(parsed.authInfo.empty() && parsed.pubKeyAuth.empty());
            CHECK(!parsed.hasErrorCode);
        });

        RunTest("Every optional TSRequest field round-trips", []() {
            Rdp::TsRequest request;
            request.version = 5;
            request.negoTokens.push_back(Rdp::Bytes(3, 0xAB));
            request.authInfo = Rdp::Bytes{0xDE, 0xAD, 0xBE, 0xEF};
            request.pubKeyAuth = Rdp::Bytes(40, 0x7F);
            request.clientNonce = Rdp::Bytes(32, 0x5A);
            Rdp::Bytes der = Rdp::EncodeTsRequest(request);
            Rdp::TsRequest parsed;
            CHECK(Rdp::ParseTsRequest(der, parsed));
            CHECK(parsed.version == 5);
            CHECK(parsed.negoTokens.size() == 1 && parsed.negoTokens[0] == request.negoTokens[0]);
            CHECK(parsed.authInfo == request.authInfo);
            CHECK(parsed.pubKeyAuth == request.pubKeyAuth);
            CHECK(parsed.clientNonce == request.clientNonce);
        });

        RunTest("An error code (even zero) round-trips distinctly from no error code", []() {
            Rdp::TsRequest withError;
            withError.hasErrorCode = true;
            withError.errorCode = 0xC0000022; // STATUS_ACCESS_DENIED, a value with the high bit set
            Rdp::TsRequest parsed;
            CHECK(Rdp::ParseTsRequest(Rdp::EncodeTsRequest(withError), parsed));
            CHECK(parsed.hasErrorCode && parsed.errorCode == 0xC0000022);

            Rdp::TsRequest zeroError;
            zeroError.hasErrorCode = true;
            zeroError.errorCode = 0;
            CHECK(Rdp::ParseTsRequest(Rdp::EncodeTsRequest(zeroError), parsed));
            CHECK(parsed.hasErrorCode && parsed.errorCode == 0);

            Rdp::TsRequest noError;
            CHECK(Rdp::ParseTsRequest(Rdp::EncodeTsRequest(noError), parsed));
            CHECK(!parsed.hasErrorCode);
        });

        RunTest("Several nego tokens keep their order", []() {
            Rdp::TsRequest request;
            request.negoTokens = {{1}, {2, 2}, {3, 3, 3}};
            Rdp::TsRequest parsed;
            CHECK(Rdp::ParseTsRequest(Rdp::EncodeTsRequest(request), parsed));
            CHECK(parsed.negoTokens.size() == 3);
            CHECK(parsed.negoTokens[0] == Rdp::Bytes{1});
            CHECK(parsed.negoTokens[1] == Rdp::Bytes({2, 2}));
            CHECK(parsed.negoTokens[2] == Rdp::Bytes({3, 3, 3}));
        });

        RunTest("A long field uses the DER long-form length and still round-trips", []() {
            Rdp::TsRequest request;
            request.pubKeyAuth = Rdp::Bytes(500, 0x33); // over 255 bytes: two length bytes
            Rdp::Bytes der = Rdp::EncodeTsRequest(request);
            Rdp::TsRequest parsed;
            CHECK(Rdp::ParseTsRequest(der, parsed));
            CHECK(parsed.pubKeyAuth == request.pubKeyAuth);
        });

        RunTest("Truncated or non-SEQUENCE DER is rejected without reading past the end", []() {
            Rdp::TsRequest request;
            request.version = 6;
            request.authInfo = Rdp::Bytes(10, 0x01);
            Rdp::Bytes der = Rdp::EncodeTsRequest(request);
            Rdp::TsRequest parsed;
            for (size_t cut = 1; cut < der.size(); ++cut)
            {
                Rdp::Bytes truncated(der.begin(), der.begin() + cut);
                Rdp::ParseTsRequest(truncated, parsed); // must not crash or read past the buffer
            }
            CHECK(!Rdp::ParseTsRequest(Rdp::Bytes{0x04, 0x01, 0x00}, parsed)); // not a SEQUENCE
            CHECK(!Rdp::ParseTsRequest({}, parsed));
        });

        RunTest("TSCredentials wraps TSPasswordCreds as credType 1 with UTF-16 fields", []() {
            Rdp::Bytes der = Rdp::EncodeTsCredentials("Domain", "User", "secret");
            CHECK(der[0] == 0x30); // outer SEQUENCE
            // credType [0] INTEGER 1 comes first: A0 03 02 01 01.
            CHECK(der[2] == 0xA0 && der[3] == 0x03 && der[4] == 0x02 && der[5] == 0x01 && der[6] == 0x01);
            // The password's bytes appear UTF-16LE ('s', 0, 'e', 0, ...) somewhere in the inner octet string.
            Rdp::Bytes needle = {'s', 0x00, 'e', 0x00, 'c', 0x00, 'r', 0x00, 'e', 0x00, 't', 0x00};
            bool found = false;
            for (size_t i = 0; i + needle.size() <= der.size(); ++i)
            {
                if (Rdp::Bytes(der.begin() + i, der.begin() + i + needle.size()) == needle)
                {
                    found = true;
                    break;
                }
            }
            CHECK(found);
        });
    }
} // namespace MarconiTest
