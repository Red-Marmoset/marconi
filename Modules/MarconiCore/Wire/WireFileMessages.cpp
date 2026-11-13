// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Turning the file transfer messages into bytes and back

#include <MarconiCore/Wire/WireFileMessages.h>

namespace Marconi
{
    namespace Wire
    {
        bool IsValidUtf8(const std::string& text)
        {
            size_t i = 0;
            const size_t size = text.size();
            while (i < size)
            {
                const uint8_t c = static_cast<uint8_t>(text[i]);
                if (c < 0x80)
                {
                    ++i;
                    continue;
                }
                size_t extra = 0;
                uint32_t value = 0;
                uint32_t minimum = 0;
                if ((c & 0xE0) == 0xC0)
                {
                    extra = 1;
                    value = c & 0x1Fu;
                    minimum = 0x80;
                }
                else if ((c & 0xF0) == 0xE0)
                {
                    extra = 2;
                    value = c & 0x0Fu;
                    minimum = 0x800;
                }
                else if ((c & 0xF8) == 0xF0)
                {
                    extra = 3;
                    value = c & 0x07u;
                    minimum = 0x10000;
                }
                else
                {
                    return false;
                }
                if (i + extra >= size) // the last byte of the sequence must exist
                {
                    return false;
                }
                for (size_t k = 1; k <= extra; ++k)
                {
                    const uint8_t next = static_cast<uint8_t>(text[i + k]);
                    if ((next & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    value = (value << 6) | (next & 0x3Fu);
                }
                if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
                {
                    return false;
                }
                i += extra + 1;
            }
            return true;
        }

        namespace
        {
            FileStatus ReadStatus(Reader& r)
            {
                uint8_t value = r.U8();
                if (value > static_cast<uint8_t>(FileStatus::BadPath))
                {
                    throw WireError("unknown file status");
                }
                return static_cast<FileStatus>(value);
            }

            // Names are shown to people and handed to the file system: refuse text that isn't UTF-8.
            std::string ReadText(Reader& r)
            {
                std::string text = r.String();
                if (!IsValidUtf8(text))
                {
                    throw WireError("text is not valid UTF-8");
                }
                return text;
            }
        } // namespace

        Bytes FileRequest::Encode() const
        {
            return Build(Type::FileRequest, [&](Writer& w) {
                w.U32(id).U8(static_cast<uint8_t>(op)).String(path).String(path2);
            });
        }

        FileRequest FileRequest::Decode(Reader& r)
        {
            FileRequest message;
            message.id = r.U32();
            uint8_t op = r.U8();
            if (op > static_cast<uint8_t>(Op::Desktop))
            {
                throw WireError("unknown file operation");
            }
            message.op = static_cast<Op>(op);
            message.path = ReadText(r);
            message.path2 = ReadText(r);
            return message;
        }

        Bytes FileResult::Encode() const
        {
            return Build(Type::FileResult, [&](Writer& w) {
                w.U32(id).U8(static_cast<uint8_t>(status));
                if (status != FileStatus::Ok)
                {
                    w.String(error);
                    return;
                }
                w.String(text);
                w.Varint(entries.size());
                for (const FileInfo& entry : entries)
                {
                    w.String(entry.name).U64(entry.size).U64(static_cast<uint64_t>(entry.modified)).U8(
                        entry.isFolder ? 1 : 0);
                }
            });
        }

        FileResult FileResult::Decode(Reader& r)
        {
            FileResult message;
            message.id = r.U32();
            message.status = ReadStatus(r);
            if (message.status != FileStatus::Ok)
            {
                message.error = ReadText(r);
                return message;
            }
            message.text = ReadText(r);
            uint64_t count = r.Varint();
            // An entry is at least 18 bytes, so a count the rest of the message can't hold is not real.
            if (count > r.Remaining() / 18 || count > kMaxListEntries)
            {
                throw WireError("more file entries than the message can hold");
            }
            message.entries.reserve(static_cast<size_t>(count));
            for (uint64_t i = 0; i < count; ++i)
            {
                FileInfo entry;
                entry.name = ReadText(r);
                entry.size = r.U64();
                entry.modified = static_cast<int64_t>(r.U64());
                entry.isFolder = r.U8() != 0;
                message.entries.push_back(std::move(entry));
            }
            return message;
        }

        Bytes FileOpen::Encode() const
        {
            return Build(Type::FileOpen,
                         [&](Writer& w) { w.U32(id).U8(static_cast<uint8_t>(mode)).String(path).U64(offset); });
        }

        FileOpen FileOpen::Decode(Reader& r)
        {
            FileOpen message;
            message.id = r.U32();
            uint8_t mode = r.U8();
            if (mode > static_cast<uint8_t>(Mode::Resume))
            {
                throw WireError("unknown file open mode");
            }
            message.mode = static_cast<Mode>(mode);
            message.path = ReadText(r);
            message.offset = r.U64();
            return message;
        }

        Bytes FileOpened::Encode() const
        {
            return Build(Type::FileOpened, [&](Writer& w) {
                w.U32(id).U8(static_cast<uint8_t>(status));
                if (status != FileStatus::Ok)
                {
                    w.String(error);
                    return;
                }
                w.U32(handle).U64(size);
            });
        }

        FileOpened FileOpened::Decode(Reader& r)
        {
            FileOpened message;
            message.id = r.U32();
            message.status = ReadStatus(r);
            if (message.status != FileStatus::Ok)
            {
                message.error = ReadText(r);
                return message;
            }
            message.handle = r.U32();
            message.size = r.U64();
            return message;
        }

        Bytes FileRead::Encode() const
        {
            return Build(Type::FileRead, [&](Writer& w) { w.U32(id).U32(handle).U64(offset).U32(length); });
        }

        FileRead FileRead::Decode(Reader& r)
        {
            FileRead message;
            message.id = r.U32();
            message.handle = r.U32();
            message.offset = r.U64();
            message.length = r.U32();
            return message;
        }

        Bytes FileData::Encode() const
        {
            return Build(Type::FileData, [&](Writer& w) {
                w.U32(id).U8(static_cast<uint8_t>(status));
                if (status != FileStatus::Ok)
                {
                    w.String(error);
                    return;
                }
                w.U8(end ? 1 : 0).Blob(data);
            });
        }

        FileData FileData::Decode(Reader& r)
        {
            FileData message;
            message.id = r.U32();
            message.status = ReadStatus(r);
            if (message.status != FileStatus::Ok)
            {
                message.error = ReadText(r);
                return message;
            }
            message.end = r.U8() != 0;
            message.data = r.Blob();
            if (message.data.size() > kMaxFileChunk)
            {
                throw WireError("a file range is larger than allowed");
            }
            return message;
        }

        Bytes FileWrite::Encode() const
        {
            return Build(Type::FileWrite, [&](Writer& w) { w.U32(handle).U64(offset).Blob(data); });
        }

        FileWrite FileWrite::Decode(Reader& r)
        {
            FileWrite message;
            message.handle = r.U32();
            message.offset = r.U64();
            message.data = r.Blob();
            if (message.data.size() > kMaxFileChunk)
            {
                throw WireError("a file range is larger than allowed");
            }
            return message;
        }

        Bytes FileClose::Encode() const
        {
            return Build(Type::FileClose, [&](Writer& w) { w.U32(id).U32(handle); });
        }

        FileClose FileClose::Decode(Reader& r)
        {
            FileClose message;
            message.id = r.U32();
            message.handle = r.U32();
            return message;
        }

        Bytes FileWriteFailed::Encode() const
        {
            return Build(Type::FileWriteFailed,
                         [&](Writer& w) { w.U32(handle).U8(static_cast<uint8_t>(status)).String(error); });
        }

        FileWriteFailed FileWriteFailed::Decode(Reader& r)
        {
            FileWriteFailed message;
            message.handle = r.U32();
            message.status = ReadStatus(r);
            message.error = ReadText(r);
            return message;
        }
    } // namespace Wire
} // namespace Marconi
