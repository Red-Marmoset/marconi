// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the server-update messages into bytes and back

#include <MarconiCore/Wire/WireUpdateMessages.h>

namespace Marconi
{
    namespace Wire
    {
        namespace
        {
            // Strings in these messages are short (names, versions, reasons): a longer one is malformed.
            constexpr size_t kMaxTextBytes = 1024;

            std::string ShortString(Reader& r)
            {
                std::string text = r.String();
                if (text.size() > kMaxTextBytes)
                {
                    throw WireError("an update message's text is too long");
                }
                return text;
            }
        } // namespace

        Bytes UpdateOffer::Encode() const
        {
            return Build(Type::UpdateOffer, [&](Writer& w) {
                w.U8(flags).String(product).String(version).String(platform).U64(size).Blob(sha256);
            });
        }

        UpdateOffer UpdateOffer::Decode(Reader& r)
        {
            UpdateOffer message;
            message.flags = r.U8();
            message.product = ShortString(r);
            message.version = ShortString(r);
            message.platform = ShortString(r);
            message.size = r.U64();
            message.sha256 = r.Blob();
            if (!message.sha256.empty() && message.sha256.size() != 32)
            {
                throw WireError("an update's SHA-256 must be 32 bytes");
            }
            return message;
        }

        Bytes UpdateAnswer::Encode() const
        {
            return Build(Type::UpdateAnswer, [&](Writer& w) {
                w.U8(static_cast<uint8_t>(status)).String(serverVersion).String(detail);
            });
        }

        UpdateAnswer UpdateAnswer::Decode(Reader& r)
        {
            UpdateAnswer message;
            uint8_t status = r.U8();
            if (status > static_cast<uint8_t>(Status::BadOffer))
            {
                throw WireError("unknown update answer");
            }
            message.status = static_cast<Status>(status);
            message.serverVersion = ShortString(r);
            message.detail = ShortString(r);
            return message;
        }

        Bytes UpdateChunk::Encode() const
        {
            return Build(Type::UpdateChunk, [&](Writer& w) { w.U64(offset).Blob(data); });
        }

        UpdateChunk UpdateChunk::Decode(Reader& r)
        {
            UpdateChunk message;
            message.offset = r.U64();
            message.data = r.Blob();
            if (message.data.size() > kUpdateChunkBytes)
            {
                throw WireError("an update piece is larger than allowed");
            }
            return message;
        }

        Bytes UpdateResult::Encode() const
        {
            return Build(Type::UpdateResult,
                         [&](Writer& w) { w.U8(static_cast<uint8_t>(status)).String(detail); });
        }

        UpdateResult UpdateResult::Decode(Reader& r)
        {
            UpdateResult message;
            uint8_t status = r.U8();
            if (status > static_cast<uint8_t>(Status::NotNewer))
            {
                throw WireError("unknown update result");
            }
            message.status = static_cast<Status>(status);
            message.detail = ShortString(r);
            return message;
        }
    } // namespace Wire
} // namespace Marconi
