// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The datagram channel's cryptography: HKDF-SHA256 key derivation, AES-256-GCM sealing of one datagram, and the
// sliding window that rejects replayed packet numbers. See SERVER_PLAN.md, "The datagram channel".
//
// The engine's Crypto module has AES-128 in ECB mode, SHA-256 and HMAC-SHA256 but no AES-GCM and no HKDF. HKDF
// (RFC 5869) is built here from Crypto::HmacSha256, as the RFC defines it; AES-GCM is mbedTLS's (gcm.h), the same
// library the engine's Crypto uses for TLS. Nothing here is a new construction.

#pragma once

#include <MarconiCore/Wire/WireStream.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        constexpr size_t kDatagramSecretBytes = 32;
        constexpr size_t kDatagramKeyBytes = 32; // AES-256
        constexpr size_t kDatagramIvBytes = 12;
        constexpr size_t kDatagramTagBytes = 16;

        // HKDF-Extract then HKDF-Expand with SHA-256 (RFC 5869). `length` is at most 255 * 32.
        Bytes HkdfSha256(const Bytes& salt, const Bytes& inputKeyMaterial, const Bytes& info, size_t length);

        // The keys for one direction of the channel: an AES-256 key and a 12-byte base nonce.
        struct DirectionKeys
        {
            std::array<uint8_t, kDatagramKeyBytes> key{};
            std::array<uint8_t, kDatagramIvBytes> iv{};
        };

        // Both directions, from the secret the server sent over the control channel. The client-to-server and
        // server-to-client keys are different, so a packet reflected back at its sender never authenticates, and
        // the two directions can both count packets from zero without ever repeating a (key, nonce) pair.
        struct DatagramKeys
        {
            DirectionKeys clientToServer;
            DirectionKeys serverToClient;
        };
        DatagramKeys DeriveDatagramKeys(const Bytes& secret, uint32_t sessionId);

        // AES-256-GCM with a fixed key. Seal and Open take the packet number, which is XORed into the base nonce
        // (as TLS 1.3 does), so a nonce is never reused while packet numbers are never reused.
        class DatagramAead
        {
        public:
            explicit DatagramAead(const DirectionKeys& keys);
            ~DatagramAead();
            DatagramAead(const DatagramAead&) = delete;
            DatagramAead& operator=(const DatagramAead&) = delete;

            bool IsValid() const { return m_valid; }
            // `out` receives plainSize + kDatagramTagBytes bytes (ciphertext then tag).
            bool Seal(uint64_t packetNumber, const uint8_t* aad, size_t aadSize, const uint8_t* plain, size_t plainSize,
                      uint8_t* out);
            // `in` is ciphertext then tag (inSize >= kDatagramTagBytes); `out` receives inSize - tag bytes. False
            // when the packet doesn't authenticate.
            bool Open(uint64_t packetNumber, const uint8_t* aad, size_t aadSize, const uint8_t* in, size_t inSize,
                      uint8_t* out);

        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
            std::array<uint8_t, kDatagramIvBytes> m_iv{};
            bool m_valid = false;
        };

        // Accepts each packet number once. Numbers older than `kSize` behind the highest seen are refused (they
        // might be replays that slid out of the window). Check before decrypting, Mark after the packet
        // authenticated, so forged packets can't move the window.
        class ReplayWindow
        {
        public:
            static constexpr uint64_t kSize = 1024;

            bool WouldAccept(uint64_t packetNumber) const;
            // Records an authenticated packet number. Returns false if it was already seen or too old.
            bool Mark(uint64_t packetNumber);
            bool HasReceived() const { return m_any; }
            uint64_t Highest() const { return m_highest; }

        private:
            bool m_any = false;
            uint64_t m_highest = 0;
            std::array<uint64_t, kSize / 64> m_bits{};
        };
    } // namespace Wire
} // namespace Marconi
