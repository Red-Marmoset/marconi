// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The CredSSP exchange (MS-CSSP 3.1.5) over TLS: NTLMv2 negotiate / challenge / authenticate, then the public-key
// binding, then the credentials. The NTLM sealing and signing follow MS-NLMP 3.4.

#include <MarconiCore/Rdp/CredSspExchange.h>

#include <MarconiCore/Rdp/CredSsp.h>
#include <MarconiCore/Rdp/Ntlm.h>
#include <MarconiCore/Rdp/NtlmSeal.h>

#include <Crypto/Crypto.h>

#include <Log/Log.h>

#include <cstring>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            enum : uint32_t
            {
                kNegotiateKeyExchange = 0x40000000,
            };

            Bytes Sha256(const Bytes& data)
            {
                Bytes out(32);
                Crypto::Sha256(data.data(), data.size(), out.data());
                return out;
            }

            // Reads one TSRequest from the TLS link: a TPKT-less, length-prefixed DER SEQUENCE. We read the outer
            // tag and length, then exactly that many bytes.
            bool ReadTsRequest(const SecurityLink& tls, TsRequest& out, std::string& error)
            {
                auto readExact = [&](uint8_t* buffer, size_t count) {
                    size_t got = 0;
                    while (got < count)
                    {
                        ptrdiff_t n = tls.receive(buffer + got, count - got);
                        if (n <= 0)
                        {
                            return false;
                        }
                        got += static_cast<size_t>(n);
                    }
                    return true;
                };
                uint8_t prefix[2];
                if (!readExact(prefix, 2) || prefix[0] != 0x30)
                {
                    error = "the server's CredSSP reply wasn't a TSRequest";
                    return false;
                }
                Bytes der = {prefix[0], prefix[1]};
                size_t length = prefix[1];
                if (length & 0x80)
                {
                    size_t count = length & 0x7F;
                    if (count == 0 || count > 4)
                    {
                        error = "the server's CredSSP reply had a bad length";
                        return false;
                    }
                    uint8_t lengthBytes[4];
                    if (!readExact(lengthBytes, count))
                    {
                        error = "the server's CredSSP reply was cut short";
                        return false;
                    }
                    length = 0;
                    for (size_t i = 0; i < count; ++i)
                    {
                        length = (length << 8) | lengthBytes[i];
                        der.push_back(lengthBytes[i]);
                    }
                }
                size_t headerSize = der.size();
                der.resize(headerSize + length);
                if (!readExact(der.data() + headerSize, length))
                {
                    error = "the server's CredSSP reply was cut short";
                    return false;
                }
                if (!ParseTsRequest(der, out))
                {
                    error = "the server's CredSSP reply didn't parse";
                    return false;
                }
                return true;
            }

            bool Send(const SecurityLink& tls, const TsRequest& request, std::string& error)
            {
                Bytes der = EncodeTsRequest(request);
                if (!tls.sendAll(der.data(), der.size()))
                {
                    error = "the CredSSP request couldn't be sent";
                    return false;
                }
                return true;
            }

            Bytes Concat(const char* magic, const Bytes& nonce, const Bytes& publicKey)
            {
                Bytes out(magic, magic + std::strlen(magic) + 1); // the magic string including its NUL
                out.insert(out.end(), nonce.begin(), nonce.end());
                out.insert(out.end(), publicKey.begin(), publicKey.end());
                return out;
            }
        } // namespace

        bool RunCredSsp(const SecurityLink& tls, const Bytes& serverPublicKey, const CredSspCredentials& credentials,
                        uint32_t& errorCode, std::string& error)
        {
            errorCode = 0;

            // Leg 1: NEGOTIATE.
            TsRequest negotiate;
            negotiate.version = 6;
            Bytes negotiateMessage = BuildNegotiateMessage();
            negotiate.negoTokens.push_back(negotiateMessage);
            if (!Send(tls, negotiate, error))
            {
                return false;
            }

            // Leg 2: the server's CHALLENGE.
            TsRequest fromServer;
            if (!ReadTsRequest(tls, fromServer, error))
            {
                return false;
            }
            if (fromServer.negoTokens.empty())
            {
                error = "the server sent no NTLM challenge";
                return false;
            }
            ChallengeMessage challenge;
            if (!ParseChallengeMessage(fromServer.negoTokens[0], challenge))
            {
                error = "the server's NTLM challenge didn't parse";
                return false;
            }
            uint32_t negotiatedVersion = fromServer.version < negotiate.version ? fromServer.version : negotiate.version;

            // Compute the NTLMv2 response and the session keys.
            uint8_t clientChallenge[8];
            Bytes nonce(32);
            if (!Crypto::RandomBytes(clientChallenge, sizeof(clientChallenge)) ||
                !Crypto::RandomBytes(nonce.data(), nonce.size()))
            {
                error = "couldn't generate random challenge material";
                return false;
            }
            uint64_t timestamp = challenge.timestamp;
            Ntlmv2Response response = ComputeNtlmv2(credentials.user, credentials.domain, credentials.password,
                                                    challenge.serverChallenge, clientChallenge, timestamp,
                                                    challenge.targetInfo);

            // Key exchange: a random session key, encrypted to the server under the base key (MS-NLMP 3.4.5.1).
            Bytes exportedSessionKey(16);
            if (!Crypto::RandomBytes(exportedSessionKey.data(), exportedSessionKey.size()))
            {
                error = "couldn't generate the session key";
                return false;
            }
            Bytes encryptedSessionKey(16);
            Crypto::Rc4(response.sessionBaseKey.data(), response.sessionBaseKey.size(), exportedSessionKey.data(),
                        encryptedSessionKey.data(), 16);

            uint32_t authFlags = challenge.flags | kNegotiateKeyExchange;
            Bytes authenticate = BuildAuthenticateMessage(credentials.user, credentials.domain, "", response.response,
                                                          encryptedSessionKey, authFlags);

            // The per-direction signing and sealing keys (MS-NLMP 3.4.5.2 / 3.4.5.3).
            Bytes clientSigning = DeriveNtlmKey(exportedSessionKey, kClientSigningMagic);
            Bytes serverSigning = DeriveNtlmKey(exportedSessionKey, kServerSigningMagic);
            Bytes clientSealing = DeriveNtlmKey(exportedSessionKey, kClientSealingMagic);
            Bytes serverSealing = DeriveNtlmKey(exportedSessionKey, kServerSealingMagic);
            NtlmProtection clientSealer(clientSigning, clientSealing);
            NtlmProtection serverSealer(serverSigning, serverSealing);

            // The public-key proof. Version 5 and up hash a magic string, the client nonce and the key; older
            // servers seal the key itself.
            Bytes pubKeyAuthData;
            if (negotiatedVersion >= 5)
            {
                pubKeyAuthData = Sha256(Concat("CredSSP Client-To-Server Binding Hash", nonce, serverPublicKey));
            }
            else
            {
                pubKeyAuthData = serverPublicKey;
            }

            // Leg 3: AUTHENTICATE plus the sealed public-key proof (and the nonce, for version 5 and up).
            TsRequest authRequest;
            authRequest.version = 6;
            authRequest.negoTokens.push_back(authenticate);
            authRequest.pubKeyAuth = clientSealer.Seal(pubKeyAuthData);
            if (negotiatedVersion >= 5)
            {
                authRequest.clientNonce = nonce;
            }
            if (!Send(tls, authRequest, error))
            {
                return false;
            }

            // Leg 4: the server's confirmation of the public key.
            if (!ReadTsRequest(tls, fromServer, error))
            {
                return false;
            }
            if (fromServer.hasErrorCode && fromServer.errorCode != 0)
            {
                errorCode = fromServer.errorCode;
                error = "the server rejected the login (status 0x" + [&]() {
                    static const char* d = "0123456789abcdef";
                    std::string h;
                    for (int i = 28; i >= 0; i -= 4)
                    {
                        h.push_back(d[(fromServer.errorCode >> i) & 0xF]);
                    }
                    return h;
                }() + ")";
                return false;
            }
            if (fromServer.pubKeyAuth.empty())
            {
                error = "the server didn't confirm the public key";
                return false;
            }
            bool verified = false;
            Bytes serverProof = serverSealer.Unseal(fromServer.pubKeyAuth, verified);
            if (!verified)
            {
                error = "the server's public-key confirmation didn't verify (a man in the middle, or a bad login)";
                return false;
            }
            Bytes expected = negotiatedVersion >= 5
                                 ? Sha256(Concat("CredSSP Server-To-Client Binding Hash", nonce, serverPublicKey))
                                 : serverPublicKey;
            if (negotiatedVersion < 5 && !expected.empty())
            {
                expected[0] = static_cast<uint8_t>(expected[0] + 1); // legacy: the server returns the key plus one
            }
            if (serverProof != expected)
            {
                error = "the server's public key didn't match the one TLS presented (a man in the middle)";
                return false;
            }

            // Leg 5: the credentials, sealed.
            TsRequest credentialsRequest;
            credentialsRequest.version = 6;
            credentialsRequest.authInfo =
                clientSealer.Seal(EncodeTsCredentials(credentials.domain, credentials.user, credentials.password));
            if (!Send(tls, credentialsRequest, error))
            {
                return false;
            }
            LOG_INFO("Rdp", "CredSSP login accepted");
            return true;
        }
    } // namespace Rdp
} // namespace Marconi
