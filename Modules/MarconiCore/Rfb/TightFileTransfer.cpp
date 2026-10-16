// Copyright (c) 2026 Red Marmoset Studios Ltd.
// TightVNC 2.x file transfer messages: encoding requests and reading replies (from TightVNC 2.8's own source)

#include <MarconiCore/Rfb/TightFileTransfer.h>

#include <cstring>
#include <functional>

namespace Marconi
{
    namespace Rfb
    {
        namespace FileTransfer
        {
            namespace
            {
                constexpr uint32_t kMaxText = 64 * 1024;
                constexpr uint32_t kMaxBlock = 64 * 1024 * 1024;

                // TightVNC's strings: a length, then UTF-8 with a terminating zero that the length counts.
                void PutText(OutBuffer& out, const std::string& text)
                {
                    out.U32(static_cast<uint32_t>(text.size() + 1)).Append(text).U8(0);
                }

                void PutU64(OutBuffer& out, uint64_t value)
                {
                    out.U32(static_cast<uint32_t>(value >> 32)).U32(static_cast<uint32_t>(value & 0xFFFFFFFFu));
                }

                std::string ReadText(InStream& in)
                {
                    uint32_t length = in.U32();
                    if (length > kMaxText)
                    {
                        throw RfbError("The server sent an invalid file name or message");
                    }
                    std::string text = in.ReadString(length);
                    while (!text.empty() && text.back() == '\0')
                    {
                        text.pop_back();
                    }
                    return text;
                }

                uint64_t ReadU64(InStream& in)
                {
                    uint64_t high = in.U32();
                    return high << 32 | in.U32();
                }

                // A block that the server may have compressed. Marconi always asks for level 0 (none).
                Bytes ReadBlock(InStream& in)
                {
                    uint8_t level = in.U8();
                    uint32_t compressed = in.U32();
                    uint32_t uncompressed = in.U32();
                    if (compressed > kMaxBlock || uncompressed > kMaxBlock)
                    {
                        throw RfbError("The server sent an invalid file transfer block");
                    }
                    Bytes data = in.ReadBytes(compressed);
                    if (level != 0)
                    {
                        throw RfbError("The server compressed file data though Marconi didn't ask it to");
                    }
                    return data;
                }

                Bytes Message(uint32_t id, const std::function<void(OutBuffer&)>& body)
                {
                    OutBuffer out;
                    out.U32(id);
                    if (body)
                    {
                        body(out);
                    }
                    return out.Data();
                }
            } // namespace

            Bytes EncodeFileListRequest(const std::string& folder)
            {
                return Message(FileListRequest, [&](OutBuffer& out) {
                    out.U8(0); // no compression
                    PutText(out, folder);
                });
            }

            Bytes EncodeDownloadStartRequest(const std::string& path)
            {
                return Message(DownloadStartRequest, [&](OutBuffer& out) {
                    PutText(out, path);
                    PutU64(out, 0); // from the start
                });
            }

            Bytes EncodeDownloadDataRequest(uint32_t size)
            {
                return Message(DownloadDataRequest, [&](OutBuffer& out) { out.U8(0).U32(size); });
            }

            Bytes EncodeUploadStartRequest(const std::string& path, bool overwrite)
            {
                return Message(UploadStartRequest, [&](OutBuffer& out) {
                    PutText(out, path);
                    out.U8(overwrite ? 1 : 0);
                    PutU64(out, 0);
                });
            }

            Bytes EncodeUploadDataRequest(const void* data, uint32_t size)
            {
                return Message(UploadDataRequest, [&](OutBuffer& out) {
                    out.U8(0).U32(size).U32(size); // uncompressed: both sizes are the same
                    out.Append(data, size);
                });
            }

            Bytes EncodeUploadEndRequest(uint64_t modifiedMilliseconds)
            {
                return Message(UploadEndRequest, [&](OutBuffer& out) {
                    out.U16(0); // file flags
                    PutU64(out, modifiedMilliseconds);
                });
            }

            Bytes EncodeMakeFolderRequest(const std::string& path)
            {
                return Message(MakeFolderRequest, [&](OutBuffer& out) { PutText(out, path); });
            }

            Bytes EncodeRemoveRequest(const std::string& path)
            {
                return Message(RemoveRequest, [&](OutBuffer& out) { PutText(out, path); });
            }

            Bytes EncodeRenameRequest(const std::string& from, const std::string& to)
            {
                return Message(RenameRequest, [&](OutBuffer& out) {
                    PutText(out, from);
                    PutText(out, to);
                });
            }

            bool IsReply(uint32_t id)
            {
                switch (id)
                {
                    case CompressionSupportReply:
                    case FileListReply:
                    case Md5Reply:
                    case UploadStartReply:
                    case UploadDataReply:
                    case UploadEndReply:
                    case DownloadStartReply:
                    case DownloadDataReply:
                    case DownloadEndReply:
                    case MakeFolderReply:
                    case RemoveReply:
                    case RenameReply:
                    case FolderSizeReply:
                    case LastRequestFailedReply: return true;
                    default: return false;
                }
            }

            Reply ReadReply(uint32_t id, InStream& in)
            {
                Reply reply;
                reply.id = id;
                switch (id)
                {
                    case CompressionSupportReply: in.U8(); break;
                    case FileListReply:
                    case DownloadDataReply: reply.data = ReadBlock(in); break;
                    case Md5Reply: in.Skip(16); break;
                    case DownloadEndReply:
                        in.U8(); // file flags
                        reply.number = ReadU64(in);
                        break;
                    case FolderSizeReply: reply.number = ReadU64(in); break;
                    case LastRequestFailedReply: reply.error = ReadText(in); break;
                    default: break; // the rest carry nothing beyond their id
                }
                return reply;
            }

            bool ParseFileList(const Bytes& data, std::vector<Files::FileEntry>& outEntries, std::string& error)
            {
                outEntries.clear();
                try
                {
                    MemorySource source(data);
                    InStream in(source);
                    uint32_t count = in.U32();
                    if (count > 1000000)
                    {
                        error = "The server sent an invalid folder listing";
                        return false;
                    }
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        Files::FileEntry entry;
                        entry.size = ReadU64(in);
                        entry.modified = static_cast<int64_t>(ReadU64(in) / 1000); // milliseconds on the wire
                        entry.isFolder = (in.U16() & FlagFolder) != 0;
                        entry.name = ReadText(in);
                        if (entry.isFolder)
                        {
                            entry.size = 0;
                        }
                        outEntries.push_back(entry);
                    }
                    return true;
                }
                catch (const RfbError&)
                {
                    error = "The server sent an invalid folder listing";
                    return false;
                }
            }
        } // namespace FileTransfer
    } // namespace Rfb
} // namespace Marconi
