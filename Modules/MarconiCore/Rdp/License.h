// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP licensing (MS-RDPELE): after the Client Info, the server sends a Server License Request and waits for the
// client's New License Request before it will send its capabilities. Marconi answers with a fresh random premaster
// secret encrypted with the server's public key, as a client with no licence does; the server then usually replies
// that the client is valid (STATUS_VALID_CLIENT). Part of RDP milestone 3 in Documentation/RDP.md.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        constexpr uint16_t kSecLicensePkt = 0x0080; // the security header flag licensing PDUs carry

        // Licensing message types (the preamble's bMsgType, MS-RDPELE 2.2.1.1).
        enum : uint8_t
        {
            kLicenseRequest = 0x01,
            kPlatformChallenge = 0x02,
            kNewLicense = 0x03,
            kUpgradeLicense = 0x04,
            kLicenseInfo = 0x12,
            kNewLicenseRequest = 0x13,
            kPlatformChallengeResponse = 0x15,
            kLicenseErrorAlert = 0xFF,
        };
        constexpr uint32_t kStatusValidClient = 0x00000007;

        // Whether `pdu` (a PDU from the I/O channel, security header first) is a licensing PDU, and which message.
        bool IsLicensingPdu(const Bytes& pdu, uint8_t& messageType);

        struct ServerLicenseRequest
        {
            Bytes serverRandom; // 32 bytes
            Bytes modulus;      // the server's RSA public key, little-endian
            Bytes exponent;     // little-endian, 4 bytes
        };

        // Parses a Server License Request PDU (security header included). The server certificate may be the
        // proprietary kind or an X.509 chain; the public key comes from it either way. False with `error` if the
        // PDU is malformed or the certificate's key can't be found.
        bool ParseServerLicenseRequest(const Bytes& pdu, ServerLicenseRequest& out, std::string& error);

        // Raw RSA as RDP licensing does it (MS-RDPELE 5.1.2.1): every value little-endian, the result as many bytes
        // as the modulus.
        Bytes RsaEncryptLittleEndian(const Bytes& input, const Bytes& modulus, const Bytes& exponent);

        // The Client New License Request (security header included) answering `request`: `clientRandom` (32
        // bytes) and `premaster` (48 bytes) are fresh random values; the names are sent as given.
        Bytes BuildNewLicenseRequest(const ServerLicenseRequest& request, const uint8_t clientRandom[32],
                                     const uint8_t premaster[48], const std::string& user,
                                     const std::string& machine);

        // Reads a Licensing Error Message (the server's verdict). STATUS_VALID_CLIENT means licensing is done.
        bool ParseLicenseErrorAlert(const Bytes& pdu, uint32_t& errorCode, uint32_t& stateTransition);
        std::string LicenseErrorText(uint32_t errorCode);
    } // namespace Rdp
} // namespace Marconi
