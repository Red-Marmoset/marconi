// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for RDP licensing: the Server License Request (with a real one xrdp sent), the RSA step, the New License
// Request, and the server's verdict.

#include "TestSupport.h"

#include <MarconiCore/Rdp/License.h>
#include <MarconiCore/Rdp/Pdu.h>

#include <string>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        uint16_t U16(const Rdp::Bytes& b, size_t o) { return static_cast<uint16_t>(b[o] | (b[o + 1] << 8)); }

        // The Server License Request xrdp 0.9 sends: a proprietary certificate with a 512-bit key.
        const char* const kXrdpLicenseRequest =
            "80003e0101023e017b3c31a6aee874f6b4a50390e7c2c739ba531c30546e9005d005ce4418918381000004002c0000004d00"
            "6900630072006f0073006f0066007400200043006f00720070006f0072006100740069006f006e0000000800000032003300"
            "360000000d000400010000000300b80001000000010000000100000006005c005253413148000000000200003f0000000100"
            "010001c7c9f78e5a38e429c300952ddd4c3e50450b0d9e2a5d186364c42cf78f29d53fc5352234ffad3ae6e39506ae5582e3"
            "c8c7b4a847c85071742953896d9ced70000000000000000008004800a8f431b9ab4be6b4f43989d6b1daf61eecb1f0543b5e"
            "3e6a71b4f775c8162f2400dee982995f330ba9a694afcb11c3f2db0942682956580156db590369db7d370000000000000000"
            "010000000e000e006d6963726f736f66742e636f6d00";
    } // namespace

    void RunRdpLicenseTests()
    {
        RunTest("xrdp's Server License Request parses: the server random and the RSA key from its certificate", []() {
            Rdp::Bytes pdu = HexBytes(kXrdpLicenseRequest);
            uint8_t type = 0;
            CHECK(Rdp::IsLicensingPdu(pdu, type) && type == Rdp::kLicenseRequest);
            Rdp::ServerLicenseRequest request;
            std::string error;
            CHECK(Rdp::ParseServerLicenseRequest(pdu, request, error));
            CHECK(request.serverRandom.size() == 32);
            CHECK(request.serverRandom[0] == 0x7b && request.serverRandom[31] == 0x81);
            CHECK(request.modulus.size() == 64 && request.modulus[0] == 0x01 && request.modulus[63] == 0x70);
            CHECK(request.exponent == Rdp::Bytes({0x01, 0x00, 0x01, 0x00}));
            // Not a licensing PDU: a plain security header.
            CHECK(!Rdp::IsLicensingPdu({0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00}, type));
            // Nor a Share Control PDU whose length happens to have the licensing bit set (xrdp's Demand Active).
            Rdp::Bytes demandActive = Rdp::WrapShareControl(Rdp::ShareControlType::DemandActive, 0x03EA,
                                                            Rdp::Bytes(0x80 - 6, 0));
            CHECK(demandActive.size() == 0x80 && !Rdp::IsLicensingPdu(demandActive, type));
            Rdp::Bytes truncated(pdu.begin(), pdu.begin() + 60);
            CHECK(!Rdp::ParseServerLicenseRequest(truncated, request, error) && !error.empty());
        });

        RunTest("An X.509 certificate chain gives the last certificate's RSA key", []() {
            // A DER fragment with the shape of a SubjectPublicKeyInfo: the rsaEncryption OID, NULL, then a BIT
            // STRING holding SEQUENCE { INTEGER modulus (with DER's sign byte), INTEGER 65537 }.
            Rdp::Bytes spki = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00,
                               0x03, 0x0f, 0x00, 0x30, 0x0c, 0x02, 0x05, 0x00, 0xc1, 0xc2, 0xc3, 0xc4, 0x02, 0x03,
                               0x01, 0x00, 0x01};
            Rdp::Bytes certificate = {0x30, 0x82, 0x00, static_cast<uint8_t>(spki.size() + 2), 0x04, 0x00};
            certificate.insert(certificate.end(), spki.begin(), spki.end());
            Rdp::Bytes blob;
            auto p32 = [&](uint32_t v) {
                for (int i = 0; i < 4; ++i)
                {
                    blob.push_back(static_cast<uint8_t>(v >> (8 * i)));
                }
            };
            p32(0x80000002); // X.509 chain, with the "temporary" flag Windows sets
            p32(2);          // two certificates: a dummy first, the server's last
            p32(3);
            blob.insert(blob.end(), {0x30, 0x01, 0x00});
            p32(static_cast<uint32_t>(certificate.size()));
            blob.insert(blob.end(), certificate.begin(), certificate.end());
            blob.insert(blob.end(), 8, 0); // padding
            // Wrap it in a License Request around nothing else: random, empty product info, no key list.
            Rdp::Bytes pdu = {0x80, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00};
            pdu.insert(pdu.end(), 32, 0xAB);
            pdu.insert(pdu.end(), {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
            pdu.insert(pdu.end(), {0x03, 0x00, static_cast<uint8_t>(blob.size()), 0x00});
            pdu.insert(pdu.end(), blob.begin(), blob.end());
            Rdp::ServerLicenseRequest request;
            std::string error;
            CHECK(Rdp::ParseServerLicenseRequest(pdu, request, error));
            CHECK(request.modulus == Rdp::Bytes({0xc4, 0xc3, 0xc2, 0xc1})); // little-endian, sign byte dropped
            CHECK(request.exponent == Rdp::Bytes({0x01, 0x00, 0x01, 0x00}));
        });

        RunTest("The RSA step works on little-endian values", []() {
            // 65 ^ 17 mod 3233 = 2790 (the textbook example: 3233 = 61 * 53).
            Rdp::Bytes out = Rdp::RsaEncryptLittleEndian({0x41}, {0xA1, 0x0C}, {0x11, 0x00, 0x00, 0x00});
            CHECK(out == Rdp::Bytes({0xE6, 0x0A}));
            // The result is always as long as the modulus, even when it is small.
            out = Rdp::RsaEncryptLittleEndian({0x01}, {0xA1, 0x0C, 0x00, 0x00}, {0x11, 0x00, 0x00, 0x00});
            CHECK(out == Rdp::Bytes({0x01, 0x00, 0x00, 0x00}));
        });

        RunTest("The New License Request carries the random, the encrypted secret and the names", []() {
            Rdp::ServerLicenseRequest request;
            std::string error;
            CHECK(Rdp::ParseServerLicenseRequest(HexBytes(kXrdpLicenseRequest), request, error));
            uint8_t clientRandom[32];
            uint8_t premaster[48];
            for (int i = 0; i < 32; ++i) clientRandom[i] = static_cast<uint8_t>(i);
            for (int i = 0; i < 48; ++i) premaster[i] = static_cast<uint8_t>(0x80 + i);
            Rdp::Bytes pdu = Rdp::BuildNewLicenseRequest(request, clientRandom, premaster, "bob", "snorkels");
            CHECK(U16(pdu, 0) == Rdp::kSecLicensePkt);
            CHECK(pdu[4] == Rdp::kNewLicenseRequest && pdu[5] == 0x03);
            CHECK(U16(pdu, 6) == pdu.size() - 4); // wMsgSize counts from the preamble
            CHECK(U16(pdu, 8) == 1);              // RSA key exchange
            CHECK(pdu[16] == 0 && pdu[16 + 31] == 31); // the client random
            size_t p = 48;
            CHECK(U16(pdu, p) == 0x0002 && U16(pdu, p + 2) == 64 + 8); // encrypted premaster: key length + padding
            // The encrypted value decrypts... can't without the private key; check it isn't the plain secret and
            // that the padding is there.
            CHECK(pdu[p + 4] != premaster[0] || pdu[p + 5] != premaster[1]);
            CHECK(pdu[p + 4 + 64] == 0 && pdu[p + 4 + 71] == 0);
            p += 4 + 72;
            CHECK(U16(pdu, p) == 0x000F && U16(pdu, p + 2) == 4);
            CHECK(std::string(reinterpret_cast<const char*>(&pdu[p + 4])) == "bob");
            p += 4 + 4;
            CHECK(U16(pdu, p) == 0x0010 && U16(pdu, p + 2) == 9);
            CHECK(std::string(reinterpret_cast<const char*>(&pdu[p + 4])) == "snorkels");
            CHECK(p + 4 + 9 == pdu.size());
        });

        RunTest("The server's licensing verdict is read", []() {
            // What xrdp answers: STATUS_VALID_CLIENT with ST_NO_TRANSITION.
            Rdp::Bytes valid = HexBytes("80000000ff0310000700000002000000281400000000");
            uint32_t code = 0;
            uint32_t transition = 0;
            CHECK(Rdp::ParseLicenseErrorAlert(valid, code, transition));
            CHECK(code == Rdp::kStatusValidClient && transition == 2);
            CHECK(Rdp::LicenseErrorText(0x02) == "The server has no Remote Desktop licence for this connection.");
            CHECK(Rdp::LicenseErrorText(0x99) == "The server reported a licensing error (code 0x00000099).");
            Rdp::Bytes notAlert = HexBytes("8000000001030000");
            CHECK(!Rdp::ParseLicenseErrorAlert(notAlert, code, transition));
        });
    }
} // namespace MarconiTest
