// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the NTLM sealing and the CredSSP exchange. The sealing and key derivation are checked directly. The
// exchange is run against a mirror server built in the test from the same public pieces: this proves the framing,
// message order and public-key verification fit together and round-trip over a link. It does NOT prove interop with
// a real server (that needs a Windows box), which no loopback here can.

#include "TestSupport.h"

#include <MarconiCore/Rdp/CredSsp.h>
#include <MarconiCore/Rdp/CredSspExchange.h>
#include <MarconiCore/Rdp/Ntlm.h>
#include <MarconiCore/Rdp/NtlmSeal.h>

#include <Crypto/Crypto.h>

#include <cstring>
#include <deque>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        Rdp::Bytes Key(uint8_t fill) { return Rdp::Bytes(16, fill); }

        // A blocking byte pipe between two threads, one SecurityLink per end.
        class Pipe
        {
        public:
            Rdp::SecurityLink End(bool a)
            {
                Rdp::SecurityLink link;
                link.sendAll = [this, a](const void* data, size_t length) {
                    const uint8_t* p = static_cast<const uint8_t*>(data);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    (a ? m_aToB : m_bToA).insert((a ? m_aToB : m_bToA).end(), p, p + length);
                    m_changed.notify_all();
                    return true;
                };
                link.receive = [this, a](void* buffer, size_t length) -> ptrdiff_t {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    std::deque<uint8_t>& from = a ? m_bToA : m_aToB;
                    if (!m_changed.wait_for(lock, std::chrono::seconds(5), [&]() { return !from.empty() || m_closed; }))
                    {
                        return -1;
                    }
                    if (from.empty())
                    {
                        return 0;
                    }
                    size_t n = std::min(length, from.size());
                    for (size_t i = 0; i < n; ++i)
                    {
                        static_cast<uint8_t*>(buffer)[i] = from.front();
                        from.pop_front();
                    }
                    return static_cast<ptrdiff_t>(n);
                };
                return link;
            }
            void Close()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_closed = true;
                m_changed.notify_all();
            }

        private:
            std::mutex m_mutex;
            std::condition_variable m_changed;
            std::deque<uint8_t> m_aToB;
            std::deque<uint8_t> m_bToA;
            bool m_closed = false;
        };

        bool ReadRequest(const Rdp::SecurityLink& link, Rdp::TsRequest& out)
        {
            uint8_t prefix[2];
            size_t got = 0;
            while (got < 2)
            {
                ptrdiff_t n = link.receive(prefix + got, 2 - got);
                if (n <= 0)
                {
                    return false;
                }
                got += static_cast<size_t>(n);
            }
            Rdp::Bytes der = {prefix[0], prefix[1]};
            size_t length = prefix[1] & 0x7F;
            if (prefix[1] & 0x80)
            {
                Rdp::Bytes lengthBytes(length);
                size_t l = 0;
                while (l < length)
                {
                    ptrdiff_t n = link.receive(lengthBytes.data() + l, length - l);
                    if (n <= 0) return false;
                    l += static_cast<size_t>(n);
                }
                size_t value = 0;
                for (uint8_t b : lengthBytes) { value = (value << 8) | b; der.push_back(b); }
                length = value;
            }
            size_t header = der.size();
            der.resize(header + length);
            size_t r = 0;
            while (r < length)
            {
                ptrdiff_t n = link.receive(der.data() + header + r, length - r);
                if (n <= 0) return false;
                r += static_cast<size_t>(n);
            }
            return Rdp::ParseTsRequest(der, out);
        }

        // A mirror "server": it plays the CredSSP legs using the same public pieces, so the client's exchange can
        // be driven to completion and verified. A fixed password is accepted.
        void MirrorServer(const Rdp::SecurityLink& link, const Rdp::Bytes& publicKey, const std::string& user,
                          const std::string& domain, const std::string& password, bool* succeeded)
        {
            *succeeded = false;
            Rdp::TsRequest request;
            if (!ReadRequest(link, request) || request.negoTokens.empty())
            {
                return;
            }
            // Send a challenge with a fixed server challenge and a target info carrying a timestamp.
            uint8_t serverChallenge[8] = {1, 2, 3, 4, 5, 6, 7, 8};
            Rdp::Bytes targetInfo = {0x02, 0x00, 0x08, 0x00, 'D', 0, 'O', 0, 'M', 0, 'N', 0, // NbDomainName
                                     0x07, 0x00, 0x08, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,          // Timestamp = 0
                                     0x00, 0x00, 0x00, 0x00};
            Rdp::Bytes challenge;
            const char* sig = "NTLMSSP";
            challenge.insert(challenge.end(), sig, sig + 8);
            auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) challenge.push_back(static_cast<uint8_t>(v >> (8 * i))); };
            auto put16 = [&](uint16_t v) { challenge.push_back(static_cast<uint8_t>(v)); challenge.push_back(static_cast<uint8_t>(v >> 8)); };
            put32(2);
            put16(0); put16(0); put32(48);
            put32(0x00080205);
            challenge.insert(challenge.end(), serverChallenge, serverChallenge + 8);
            put32(0); put32(0);
            put16(static_cast<uint16_t>(targetInfo.size())); put16(static_cast<uint16_t>(targetInfo.size())); put32(48);
            challenge.insert(challenge.end(), targetInfo.begin(), targetInfo.end());

            Rdp::TsRequest challengeRequest;
            challengeRequest.version = 6;
            challengeRequest.negoTokens.push_back(challenge);
            Rdp::Bytes der = Rdp::EncodeTsRequest(challengeRequest);
            link.sendAll(der.data(), der.size());

            // Leg 3: read the authenticate + sealed public key. Recompute the session keys the same way the client
            // did (it echoes the encrypted session key in the AUTHENTICATE message).
            if (!ReadRequest(link, request) || request.negoTokens.empty() || request.pubKeyAuth.empty())
            {
                return;
            }
            const Rdp::Bytes& auth = request.negoTokens[0];
            // Pull the NtChallengeResponse and EncryptedRandomSessionKey out of the AUTHENTICATE message by its
            // field descriptors (offsets 20 and 52).
            auto field = [&](size_t at, Rdp::Bytes& into) {
                uint16_t len = static_cast<uint16_t>(auth[at]) | (static_cast<uint16_t>(auth[at + 1]) << 8);
                uint32_t off = 0;
                for (int i = 0; i < 4; ++i) off |= static_cast<uint32_t>(auth[at + 4 + i]) << (8 * i);
                into.assign(auth.begin() + off, auth.begin() + off + len);
            };
            Rdp::Bytes ntResponse, encryptedKey;
            field(20, ntResponse);
            field(52, encryptedKey);
            // NTProofStr is the first 16 bytes; recompute the base key from the password we expect.
            Rdp::Bytes ntowf2 = Rdp::NtowfV2(user, domain, password);
            Rdp::Bytes ntProof(ntResponse.begin(), ntResponse.begin() + 16);
            Rdp::Bytes sessionBaseKey(16);
            Crypto::HmacMd5(ntowf2.data(), ntowf2.size(), ntProof.data(), 16, sessionBaseKey.data());
            Rdp::Bytes exportedKey(16);
            Crypto::Rc4(sessionBaseKey.data(), 16, encryptedKey.data(), exportedKey.data(), 16);

            Rdp::NtlmProtection clientSide(Rdp::DeriveNtlmKey(exportedKey, Rdp::kClientSigningMagic),
                                           Rdp::DeriveNtlmKey(exportedKey, Rdp::kClientSealingMagic));
            Rdp::NtlmProtection serverSide(Rdp::DeriveNtlmKey(exportedKey, Rdp::kServerSigningMagic),
                                           Rdp::DeriveNtlmKey(exportedKey, Rdp::kServerSealingMagic));
            bool ok = false;
            Rdp::Bytes clientProof = clientSide.Unseal(request.pubKeyAuth, ok);
            if (!ok)
            {
                return;
            }
            const std::string clientMagic = "CredSSP Client-To-Server Binding Hash";
            Rdp::Bytes magic(clientMagic.begin(), clientMagic.end());
            magic.push_back(0);
            magic.insert(magic.end(), request.clientNonce.begin(), request.clientNonce.end());
            magic.insert(magic.end(), publicKey.begin(), publicKey.end());
            Rdp::Bytes expected(32);
            Crypto::Sha256(magic.data(), magic.size(), expected.data());
            if (clientProof != expected)
            {
                return;
            }
            // Leg 4: return the server-side binding hash, sealed.
            const std::string serverMagicText = "CredSSP Server-To-Client Binding Hash";
            Rdp::Bytes serverMagic(serverMagicText.begin(), serverMagicText.end());
            serverMagic.push_back(0);
            serverMagic.insert(serverMagic.end(), request.clientNonce.begin(), request.clientNonce.end());
            serverMagic.insert(serverMagic.end(), publicKey.begin(), publicKey.end());
            Rdp::Bytes serverProof(32);
            Crypto::Sha256(serverMagic.data(), serverMagic.size(), serverProof.data());
            Rdp::TsRequest confirm;
            confirm.version = 6;
            confirm.pubKeyAuth = serverSide.Seal(serverProof);
            der = Rdp::EncodeTsRequest(confirm);
            link.sendAll(der.data(), der.size());

            // Leg 5: the sealed credentials.
            if (!ReadRequest(link, request) || request.authInfo.empty())
            {
                return;
            }
            Rdp::Bytes creds = clientSide.Unseal(request.authInfo, ok);
            *succeeded = ok && !creds.empty() && creds[0] == 0x30; // a TSCredentials SEQUENCE
        }
    } // namespace

    void RunRdpCredSspExchangeTests()
    {
        RunTest("NTLM sealing round-trips and detects tampering, in lockstep across two ends", []() {
            Rdp::NtlmProtection sender(Key(0x11), Key(0x22));
            Rdp::NtlmProtection receiver(Key(0x11), Key(0x22));
            Rdp::Bytes message = {'h', 'e', 'l', 'l', 'o'};
            Rdp::Bytes sealed = sender.Seal(message);
            CHECK(sealed.size() == 16 + message.size());
            CHECK(Rdp::Bytes(sealed.begin() + 16, sealed.end()) != message); // the data is encrypted
            bool ok = false;
            CHECK(receiver.Unseal(sealed, ok) == message && ok);

            // A second message keeps the keystream moving: a fresh receiver in lockstep still reads it.
            Rdp::NtlmProtection sender2(Key(0x11), Key(0x22));
            Rdp::NtlmProtection receiver2(Key(0x11), Key(0x22));
            sender2.Seal({1, 2, 3});
            receiver2.Unseal(sender2.Seal({4, 5, 6}), ok); // first sealed/unsealed to advance both
            // (the lockstep is covered by the exchange test below; here just check tampering)
            Rdp::Bytes tampered = sender.Seal(message);
            tampered[16] ^= 0xFF;
            bool ok2 = true;
            receiver.Unseal(tampered, ok2);
            CHECK(!ok2);
        });

        RunTest("The four NTLM keys are deterministic and distinct", []() {
            Rdp::Bytes session = Key(0x5A);
            CHECK(Rdp::DeriveNtlmKey(session, Rdp::kClientSigningMagic) == Rdp::DeriveNtlmKey(session, Rdp::kClientSigningMagic));
            CHECK(Rdp::DeriveNtlmKey(session, Rdp::kClientSigningMagic).size() == 16);
            Rdp::Bytes a = Rdp::DeriveNtlmKey(session, Rdp::kClientSigningMagic);
            Rdp::Bytes b = Rdp::DeriveNtlmKey(session, Rdp::kServerSigningMagic);
            Rdp::Bytes c = Rdp::DeriveNtlmKey(session, Rdp::kClientSealingMagic);
            Rdp::Bytes d = Rdp::DeriveNtlmKey(session, Rdp::kServerSealingMagic);
            CHECK(a != b && a != c && a != d && b != c && b != d && c != d);
        });

        RunTest("The CredSSP exchange completes against a mirror server and binds to the public key", []() {
            auto pipe = std::make_shared<Pipe>();
            Rdp::Bytes publicKey(64, 0xAB); // stands in for the TLS SubjectPublicKeyInfo
            bool serverOk = false;
            std::thread server([&]() {
                MirrorServer(pipe->End(false), publicKey, "alice", "WORK", "secret", &serverOk);
            });
            Rdp::CredSspCredentials credentials{"WORK", "alice", "secret"};
            uint32_t errorCode = 0;
            std::string error;
            bool clientOk = Rdp::RunCredSsp(pipe->End(true), publicKey, credentials, errorCode, error);
            pipe->Close();
            server.join();
            CHECK(clientOk);
            if (!clientOk)
            {
                printf("  client: %s\n", error.c_str());
            }
            CHECK(serverOk);
        });

        RunTest("The exchange fails when the server reports an error status", []() {
            auto pipe = std::make_shared<Pipe>();
            std::thread server([&]() {
                Rdp::SecurityLink link = pipe->End(false);
                Rdp::TsRequest request;
                ReadRequest(link, request); // the NEGOTIATE
                Rdp::TsRequest reject;
                reject.version = 6;
                reject.hasErrorCode = true;
                reject.errorCode = 0xC000006D; // STATUS_LOGON_FAILURE
                // A challenge is still needed before the client will look at an error; send error on leg 2 is
                // non-standard, so instead send a minimal challenge then the error on leg 4.
                uint8_t serverChallenge[8] = {1, 2, 3, 4, 5, 6, 7, 8};
                Rdp::Bytes challenge;
                const char* sig = "NTLMSSP";
                challenge.insert(challenge.end(), sig, sig + 8);
                auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) challenge.push_back(static_cast<uint8_t>(v >> (8 * i))); };
                auto put16 = [&](uint16_t v) { challenge.push_back(static_cast<uint8_t>(v)); challenge.push_back(static_cast<uint8_t>(v >> 8)); };
                put32(2); put16(0); put16(0); put32(48); put32(0x00080205);
                challenge.insert(challenge.end(), serverChallenge, serverChallenge + 8);
                put32(0); put32(0); put16(0); put16(0); put32(48);
                Rdp::TsRequest challengeRequest;
                challengeRequest.version = 6;
                challengeRequest.negoTokens.push_back(challenge);
                Rdp::Bytes der = Rdp::EncodeTsRequest(challengeRequest);
                link.sendAll(der.data(), der.size());
                ReadRequest(link, request); // the AUTHENTICATE
                der = Rdp::EncodeTsRequest(reject);
                link.sendAll(der.data(), der.size());
            });
            Rdp::CredSspCredentials credentials{"WORK", "alice", "wrong"};
            uint32_t errorCode = 0;
            std::string error;
            bool clientOk = Rdp::RunCredSsp(pipe->End(true), Rdp::Bytes(64, 0xAB), credentials, errorCode, error);
            pipe->Close();
            server.join();
            CHECK(!clientOk);
            CHECK(errorCode == 0xC000006D);
        });
    }
} // namespace MarconiTest
