// Copyright (c) 2026 Red Marmoset Studios Ltd.
// TightVNC 2.x file transfer messages: encoding requests and reading replies (from TightVNC 2.8's own source)

#pragma once

#include <MarconiCore/Files/FileSource.h>
#include <MarconiCore/Rfb/Stream.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rfb
    {
        namespace FileTransfer
        {
            // Every message starts with one of these 4-byte ids; the first byte is always 0xFC.
            constexpr uint32_t CompressionSupportRequest = 0xFC000100;
            constexpr uint32_t CompressionSupportReply = 0xFC000101;
            constexpr uint32_t FileListRequest = 0xFC000102;
            constexpr uint32_t FileListReply = 0xFC000103;
            constexpr uint32_t Md5Request = 0xFC000104;
            constexpr uint32_t Md5Reply = 0xFC000105;
            constexpr uint32_t UploadStartRequest = 0xFC000106;
            constexpr uint32_t UploadStartReply = 0xFC000107;
            constexpr uint32_t UploadDataRequest = 0xFC000108;
            constexpr uint32_t UploadDataReply = 0xFC000109;
            constexpr uint32_t UploadEndRequest = 0xFC00010A;
            constexpr uint32_t UploadEndReply = 0xFC00010B;
            constexpr uint32_t DownloadStartRequest = 0xFC00010C;
            constexpr uint32_t DownloadStartReply = 0xFC00010D;
            constexpr uint32_t DownloadDataRequest = 0xFC00010E;
            constexpr uint32_t DownloadDataReply = 0xFC00010F;
            constexpr uint32_t DownloadEndReply = 0xFC000110;
            constexpr uint32_t MakeFolderRequest = 0xFC000111;
            constexpr uint32_t MakeFolderReply = 0xFC000112;
            constexpr uint32_t RemoveRequest = 0xFC000113;
            constexpr uint32_t RemoveReply = 0xFC000114;
            constexpr uint32_t RenameRequest = 0xFC000115;
            constexpr uint32_t RenameReply = 0xFC000116;
            constexpr uint32_t FolderSizeRequest = 0xFC000117;
            constexpr uint32_t FolderSizeReply = 0xFC000118;
            constexpr uint32_t LastRequestFailedReply = 0xFC000119;

            constexpr uint16_t FlagFolder = 0x1;

            // A reply, read by the connection thread.
            struct Reply
            {
                uint32_t id = 0;
                Bytes data;        // FileList: the listing to parse; DownloadData: file bytes
                std::string error; // LastRequestFailed: why
                uint64_t number = 0; // FolderSize: bytes; DownloadEnd: modification time
            };

            Bytes EncodeFileListRequest(const std::string& folder);
            Bytes EncodeDownloadStartRequest(const std::string& path);
            Bytes EncodeDownloadDataRequest(uint32_t size);
            Bytes EncodeUploadStartRequest(const std::string& path, bool overwrite);
            Bytes EncodeUploadDataRequest(const void* data, uint32_t size);
            Bytes EncodeUploadEndRequest(uint64_t modifiedMilliseconds);
            Bytes EncodeMakeFolderRequest(const std::string& path);
            Bytes EncodeRemoveRequest(const std::string& path);
            Bytes EncodeRenameRequest(const std::string& from, const std::string& to);

            // Whether the connection thread knows how to read the reply with this id.
            bool IsReply(uint32_t id);
            // Reads the rest of a reply whose id has just been read. Throws RfbError on bad data.
            Reply ReadReply(uint32_t id, InStream& in);
            // The listing in a FileList reply.
            bool ParseFileList(const Bytes& data, std::vector<Files::FileEntry>& outEntries, std::string& error);
        } // namespace FileTransfer
    } // namespace Rfb
} // namespace Marconi
