// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encoding and decoding of the datagram channel's control messages

#include <MarconiCore/Wire/DatagramCrypto.h>
#include <MarconiCore/Wire/DatagramMessages.h>

#include <Crypto/Crypto.h>

namespace Marconi
{
    namespace Wire
    {
        bool DatagramOffer::Create(uint16_t port, uint16_t mtu, DatagramOffer& out)
        {
            uint8_t id[4];
            out = DatagramOffer();
            out.secret.assign(kDatagramSecretBytes, 0);
            if (!Crypto::RandomBytes(id, sizeof(id)) || !Crypto::RandomBytes(out.secret.data(), out.secret.size()))
            {
                return false;
            }
            out.sessionId = static_cast<uint32_t>(id[0]) | (static_cast<uint32_t>(id[1]) << 8) |
                            (static_cast<uint32_t>(id[2]) << 16) | (static_cast<uint32_t>(id[3]) << 24);
            out.port = port;
            out.mtu = mtu;
            return true;
        }

        Bytes DatagramOffer::Encode() const
        {
            return Build(Type::DatagramOffer,
                         [&](Writer& w) { w.U8(version).U32(sessionId).U16(port).U16(mtu).Blob(secret); });
        }

        DatagramOffer DatagramOffer::Decode(Reader& r)
        {
            DatagramOffer message;
            message.version = r.U8();
            message.sessionId = r.U32();
            message.port = r.U16();
            message.mtu = r.U16();
            message.secret = r.Blob();
            if (message.secret.size() != kDatagramSecretBytes)
            {
                throw WireError("DatagramOffer: the secret is not " + std::to_string(kDatagramSecretBytes) + " bytes");
            }
            return message;
        }

        Bytes DatagramAccept::Encode() const
        {
            return Build(Type::DatagramAccept, [&](Writer& w) { w.U32(sessionId).U8(accepted ? 1 : 0); });
        }

        DatagramAccept DatagramAccept::Decode(Reader& r)
        {
            DatagramAccept message;
            message.sessionId = r.U32();
            message.accepted = r.U8() != 0;
            return message;
        }

        Bytes DatagramState::Encode() const
        {
            return Build(Type::DatagramState, [&](Writer& w) { w.U32(sessionId).U8(working ? 1 : 0); });
        }

        DatagramState DatagramState::Decode(Reader& r)
        {
            DatagramState message;
            message.sessionId = r.U32();
            message.working = r.U8() != 0;
            return message;
        }

        Bytes DatagramData::Encode() const
        {
            return Build(Type::DatagramData, [&](Writer& w) { w.U32(sessionId).U8(channel).U32(messageId).Blob(payload); });
        }

        DatagramData DatagramData::Decode(Reader& r)
        {
            DatagramData message;
            message.sessionId = r.U32();
            message.channel = r.U8();
            message.messageId = r.U32();
            message.payload = r.Blob();
            return message;
        }
    } // namespace Wire
} // namespace Marconi
