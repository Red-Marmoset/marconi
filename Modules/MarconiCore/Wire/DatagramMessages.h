// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The control-channel messages that set up and back up the datagram channel (types 90 to 99): the server's offer
// of a UDP port with the secret the keys come from, the client's answer, the "UDP is blocked" notice, and a
// datagram-channel message carried over the control channel when UDP isn't usable.

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

namespace Marconi
{
    namespace Wire
    {
        constexpr uint8_t kDatagramVersion = 1;

        // Server -> client, inside TLS, after login. The secret is never sent anywhere else: the UDP packets carry
        // only the session id. Keys are derived from it (see DeriveDatagramKeys).
        struct DatagramOffer
        {
            uint8_t version = kDatagramVersion;
            uint32_t sessionId = 0;
            uint16_t port = 0; // the server's UDP port; the client sends to the address it connected to
            uint16_t mtu = 1200; // largest UDP payload either end sends
            Bytes secret;      // kDatagramSecretBytes random bytes

            // A fresh offer: random session id and secret from the OS. False if the OS random source failed.
            static bool Create(uint16_t port, uint16_t mtu, DatagramOffer& out);

            Bytes Encode() const;
            static DatagramOffer Decode(Reader& reader);
        };

        struct DatagramAccept
        {
            uint32_t sessionId = 0;
            bool accepted = true;

            Bytes Encode() const;
            static DatagramAccept Decode(Reader& reader);
        };

        // "UDP is blocked, send everything over the control channel" (working = false), or "UDP works again".
        struct DatagramState
        {
            uint32_t sessionId = 0;
            bool working = false;

            Bytes Encode() const;
            static DatagramState Decode(Reader& reader);
        };

        // A datagram-channel message sent over TCP: used while UDP is blocked and for reliable channels.
        struct DatagramData
        {
            uint32_t sessionId = 0;
            uint8_t channel = 0;
            uint32_t messageId = 0;
            Bytes payload;

            Bytes Encode() const;
            static DatagramData Decode(Reader& reader);
        };
    } // namespace Wire
} // namespace Marconi
