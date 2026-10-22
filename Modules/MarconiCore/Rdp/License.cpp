// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP licensing: the Server License Request and the client's New License Request.

#include <MarconiCore/Rdp/License.h>

#include <Crypto/BigUInt.h>

#include <algorithm>
#include <cstdio>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            uint16_t GetU16(const Bytes& data, size_t offset)
            {
                return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            }
            uint32_t GetU32(const Bytes& data, size_t offset)
            {
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
                }
                return value;
            }
            void PutU16(Bytes& out, uint16_t value)
            {
                out.push_back(static_cast<uint8_t>(value & 0xFF));
                out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            }
            void PutU32(Bytes& out, uint32_t value)
            {
                for (int i = 0; i < 4; ++i)
                {
                    out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
                }
            }

            // Licensing binary blob types (MS-RDPELE 2.2.1.12.1).
            enum : uint16_t
            {
                kBlobRandom = 0x0002,
                kBlobCertificate = 0x0003,
                kBlobKeyExchangeAlg = 0x000D,
                kBlobClientUserName = 0x000F,
                kBlobClientMachineName = 0x0010,
            };
            constexpr size_t kPreambleEnd = 8; // security header (4) + bMsgType, bVersion, wMsgSize

            // A DER length at `p` (advanced past it); SIZE_MAX if malformed.
            size_t ReadDerLength(const Bytes& data, size_t& p)
            {
                if (p >= data.size())
                {
                    return SIZE_MAX;
                }
                uint8_t first = data[p++];
                if ((first & 0x80) == 0)
                {
                    return first;
                }
                size_t count = first & 0x7F;
                if (count == 0 || count > 4 || p + count > data.size())
                {
                    return SIZE_MAX;
                }
                size_t length = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    length = (length << 8) | data[p++];
                }
                return length;
            }

            // Finds the RSA public key in a DER certificate: the rsaEncryption algorithm identifier, then the BIT
            // STRING holding SEQUENCE { INTEGER modulus, INTEGER exponent }. Big-endian values out.
            bool FindRsaKeyInDer(const Bytes& der, Bytes& modulus, Bytes& exponent)
            {
                static const uint8_t kRsaOid[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
                auto at = std::search(der.begin(), der.end(), std::begin(kRsaOid), std::end(kRsaOid));
                while (at != der.end())
                {
                    size_t p = static_cast<size_t>(at - der.begin()) + sizeof(kRsaOid);
                    if (p + 2 <= der.size() && der[p] == 0x05 && der[p + 1] == 0x00)
                    {
                        p += 2; // the NULL parameters
                    }
                    if (p < der.size() && der[p] == 0x03) // BIT STRING
                    {
                        ++p;
                        size_t bitStringLength = ReadDerLength(der, p);
                        if (bitStringLength != SIZE_MAX && p < der.size() && der[p] == 0x00)
                        {
                            ++p; // unused bits
                            if (p < der.size() && der[p] == 0x30)
                            {
                                ++p;
                                size_t sequenceLength = ReadDerLength(der, p);
                                Bytes values[2];
                                bool ok = sequenceLength != SIZE_MAX;
                                for (int i = 0; ok && i < 2; ++i)
                                {
                                    if (p >= der.size() || der[p] != 0x02)
                                    {
                                        ok = false;
                                        break;
                                    }
                                    ++p;
                                    size_t length = ReadDerLength(der, p);
                                    if (length == SIZE_MAX || length == 0 || p + length > der.size())
                                    {
                                        ok = false;
                                        break;
                                    }
                                    values[i].assign(der.begin() + p, der.begin() + p + length);
                                    p += length;
                                }
                                if (ok)
                                {
                                    modulus = values[0];
                                    exponent = values[1];
                                    while (modulus.size() > 1 && modulus.front() == 0)
                                    {
                                        modulus.erase(modulus.begin()); // DER's sign byte
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                    at = std::search(at + 1, der.end(), std::begin(kRsaOid), std::end(kRsaOid));
                }
                return false;
            }

            bool ParseCertificate(const Bytes& blob, ServerLicenseRequest& out, std::string& error)
            {
                if (blob.size() < 4)
                {
                    error = "the server's licensing certificate is too short";
                    return false;
                }
                uint32_t version = GetU32(blob, 0) & 0x7FFFFFFF;
                if (version == 1)
                {
                    // PROPRIETARYSERVERCERTIFICATE: dwSigAlgId, dwKeyAlgId, wPublicKeyBlobType, wPublicKeyBlobLen,
                    // then RSA_PUBLIC_KEY: magic "RSA1", keylen, bitlen, datalen, pubExp, modulus (keylen bytes,
                    // little-endian, with 8 bytes of padding after the bitlen/8 that matter).
                    if (blob.size() < 16 + 20)
                    {
                        error = "the server's licensing certificate is too short";
                        return false;
                    }
                    size_t p = 16;
                    uint32_t keyLength = GetU32(blob, p + 4);
                    uint32_t bitLength = GetU32(blob, p + 8);
                    out.exponent.assign(blob.begin() + p + 16, blob.begin() + p + 20);
                    size_t modulusBytes = bitLength / 8;
                    if (blob[p] != 'R' || blob[p + 1] != 'S' || blob[p + 2] != 'A' || blob[p + 3] != '1' ||
                        modulusBytes == 0 || modulusBytes > keyLength || p + 20 + keyLength > blob.size())
                    {
                        error = "the server's licensing certificate has no RSA key";
                        return false;
                    }
                    out.modulus.assign(blob.begin() + p + 20, blob.begin() + p + 20 + modulusBytes);
                    return true;
                }
                if (version == 2)
                {
                    // X509_CERTIFICATE_CHAIN: NumCertBlobs, then each cbCert and the DER certificate. The last one
                    // is the server's.
                    if (blob.size() < 8)
                    {
                        error = "the server's licensing certificate chain is too short";
                        return false;
                    }
                    uint32_t count = GetU32(blob, 4);
                    size_t p = 8;
                    Bytes last;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (p + 4 > blob.size())
                        {
                            error = "the server's licensing certificate chain is truncated";
                            return false;
                        }
                        uint32_t length = GetU32(blob, p);
                        p += 4;
                        if (p + length > blob.size())
                        {
                            error = "the server's licensing certificate chain is truncated";
                            return false;
                        }
                        last.assign(blob.begin() + p, blob.begin() + p + length);
                        p += length;
                    }
                    Bytes modulus;
                    Bytes exponent;
                    if (last.empty() || !FindRsaKeyInDer(last, modulus, exponent))
                    {
                        error = "the server's licensing certificate has no RSA key Marconi can read";
                        return false;
                    }
                    out.modulus.assign(modulus.rbegin(), modulus.rend());
                    out.exponent.assign(exponent.rbegin(), exponent.rend());
                    out.exponent.resize(4, 0);
                    return true;
                }
                error = "the server's licensing certificate is of a kind Marconi doesn't know";
                return false;
            }
        } // namespace

        bool IsLicensingPdu(const Bytes& pdu, uint8_t& messageType)
        {
            if (pdu.size() < kPreambleEnd)
            {
                return false;
            }
            // Under TLS the other PDUs have no security header, so a Share Control PDU starts with its own length
            // and a pduType whose version bits are 1: tell those apart before trusting the licensing flag.
            if (GetU16(pdu, 0) == pdu.size() && (GetU16(pdu, 2) & 0x00F0) == 0x0010)
            {
                return false;
            }
            if ((GetU16(pdu, 0) & kSecLicensePkt) == 0)
            {
                return false;
            }
            messageType = pdu[4];
            return true;
        }

        bool ParseServerLicenseRequest(const Bytes& pdu, ServerLicenseRequest& out, std::string& error)
        {
            uint8_t type = 0;
            if (!IsLicensingPdu(pdu, type) || type != kLicenseRequest)
            {
                error = "not a Server License Request";
                return false;
            }
            size_t p = kPreambleEnd;
            if (p + 32 + 12 > pdu.size())
            {
                error = "the Server License Request is too short";
                return false;
            }
            out.serverRandom.assign(pdu.begin() + p, pdu.begin() + p + 32);
            p += 32;
            // ProductInfo: dwVersion, cbCompanyName, pbCompanyName, cbProductId, pbProductId.
            uint32_t companyLength = GetU32(pdu, p + 4);
            p += 8;
            if (p + companyLength + 4 > pdu.size())
            {
                error = "the Server License Request's product info is truncated";
                return false;
            }
            p += companyLength;
            uint32_t productLength = GetU32(pdu, p);
            p += 4 + productLength;
            // Then blobs: the key exchange list, the certificate, and (not a blob) the scope list.
            bool haveCertificate = false;
            while (p + 4 <= pdu.size())
            {
                uint16_t blobType = GetU16(pdu, p);
                uint16_t blobLength = GetU16(pdu, p + 2);
                p += 4;
                if (p + blobLength > pdu.size())
                {
                    error = "a blob in the Server License Request is truncated";
                    return false;
                }
                if (blobType == kBlobCertificate)
                {
                    Bytes certificate(pdu.begin() + p, pdu.begin() + p + blobLength);
                    if (!ParseCertificate(certificate, out, error))
                    {
                        return false;
                    }
                    haveCertificate = true;
                    break;
                }
                if (blobType != kBlobKeyExchangeAlg)
                {
                    break; // the scope list starts with a count, not a blob header
                }
                p += blobLength;
            }
            if (!haveCertificate)
            {
                error = "the Server License Request carries no certificate";
                return false;
            }
            return true;
        }

        Bytes RsaEncryptLittleEndian(const Bytes& input, const Bytes& modulus, const Bytes& exponent)
        {
            Bytes inputBe(input.rbegin(), input.rend());
            Bytes modulusBe(modulus.rbegin(), modulus.rend());
            Bytes exponentBe(exponent.rbegin(), exponent.rend());
            Crypto::BigUInt n = Crypto::BigUInt::FromBytes(modulusBe);
            if (n.IsZero())
            {
                return Bytes(modulus.size(), 0);
            }
            Crypto::BigUInt result = Crypto::BigUInt::ModPow(Crypto::BigUInt::FromBytes(inputBe),
                                                             Crypto::BigUInt::FromBytes(exponentBe), n);
            Bytes resultBe = result.ToBytes(modulus.size());
            if (resultBe.size() > modulus.size())
            {
                resultBe.erase(resultBe.begin(), resultBe.begin() + (resultBe.size() - modulus.size()));
            }
            return Bytes(resultBe.rbegin(), resultBe.rend());
        }

        Bytes BuildNewLicenseRequest(const ServerLicenseRequest& request, const uint8_t clientRandom[32],
                                     const uint8_t premaster[48], const std::string& user,
                                     const std::string& machine)
        {
            Bytes body;
            PutU32(body, 1);          // dwPreferredKeyExchangeAlg: KEY_EXCHANGE_ALG_RSA
            PutU32(body, 0x04010000); // dwPlatformId: a Windows client image, as every client says
            body.insert(body.end(), clientRandom, clientRandom + 32);
            // EncryptedPreMasterSecret: the RSA result, then 8 bytes of padding, as a BB_RANDOM_BLOB.
            Bytes encrypted = RsaEncryptLittleEndian(Bytes(premaster, premaster + 48), request.modulus,
                                                     request.exponent);
            encrypted.insert(encrypted.end(), 8, 0);
            PutU16(body, kBlobRandom);
            PutU16(body, static_cast<uint16_t>(encrypted.size()));
            body.insert(body.end(), encrypted.begin(), encrypted.end());
            auto putName = [&body](uint16_t type, const std::string& name) {
                PutU16(body, type);
                PutU16(body, static_cast<uint16_t>(name.size() + 1));
                body.insert(body.end(), name.begin(), name.end());
                body.push_back(0);
            };
            putName(kBlobClientUserName, user);
            putName(kBlobClientMachineName, machine);

            Bytes out;
            PutU16(out, kSecLicensePkt);
            PutU16(out, 0); // flagsHi
            out.push_back(kNewLicenseRequest);
            out.push_back(0x03); // bVersion: PREAMBLE_VERSION_3_0
            PutU16(out, static_cast<uint16_t>(body.size() + 4));
            out.insert(out.end(), body.begin(), body.end());
            return out;
        }

        bool ParseLicenseErrorAlert(const Bytes& pdu, uint32_t& errorCode, uint32_t& stateTransition)
        {
            uint8_t type = 0;
            if (!IsLicensingPdu(pdu, type) || type != kLicenseErrorAlert || pdu.size() < kPreambleEnd + 8)
            {
                return false;
            }
            errorCode = GetU32(pdu, kPreambleEnd);
            stateTransition = GetU32(pdu, kPreambleEnd + 4);
            return true;
        }

        std::string LicenseErrorText(uint32_t errorCode)
        {
            switch (errorCode)
            {
                case 0x00000001: return "The server rejected its own licensing certificate.";
                case 0x00000002: return "The server has no Remote Desktop licence for this connection.";
                case 0x00000003: return "The server found the licensing message damaged.";
                case 0x00000004: return "The server's licensing scope doesn't cover this connection.";
                case 0x00000006: return "The server can't reach its licensing server.";
                case 0x00000008: return "The server refused this client in licensing.";
                case 0x0000000B: return "The server refused the licensing product id.";
                case 0x0000000C: return "The server found a licensing message the wrong length.";
                default: break;
            }
            char code[32];
            snprintf(code, sizeof(code), "0x%08x", errorCode);
            return std::string("The server reported a licensing error (code ") + code + ").";
        }
    } // namespace Rdp
} // namespace Marconi
