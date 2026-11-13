// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi protocol's file transfer messages: browsing the other computer's folders and copying files both ways
//
// Requests carry an id the answer repeats, so several can be in flight at once (the file panel browses while the
// transfer queue copies). File contents move in ranges: the client asks for a range of an open file (FileRead) or
// sends one (FileWrite), so a copy can start part-way through a file, which is how an interrupted copy resumes.
// Paths use '/' everywhere, as in Files::FileSource.

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Wire
    {
        // The largest range one FileRead asks for or one FileWrite carries. A peer that goes past it gets an error.
        constexpr uint32_t kMaxFileChunk = 1024 * 1024;
        // The most entries one folder listing can hold; a bigger folder is refused rather than sent in pieces.
        constexpr uint32_t kMaxListEntries = 200000;

        // True if `text` is well-formed UTF-8 (no overlong forms, surrogates or values past U+10FFFF).
        bool IsValidUtf8(const std::string& text);

        // How a file request went. Everything but Ok comes with a sentence for the person.
        enum class FileStatus : uint8_t
        {
            Ok = 0,
            Failed = 1,     // the computer said no (not found, in use, disk full...)
            NotAllowed = 2, // the server doesn't share its files
            BadPath = 3     // the path can't be used (outside what is shared, or not a valid path)
        };

        struct FileInfo
        {
            std::string name; // without the folder
            uint64_t size = 0;
            int64_t modified = 0; // seconds since 1970 (UTC); 0 if unknown
            bool isFolder = false;
        };

        struct FileRequest
        {
            enum class Op : uint8_t
            {
                Home = 0,        // answered with `text`: the folder to start in
                List = 1,        // `path`: answered with `entries`
                Stat = 2,        // `path`: answered with one entry
                MakeFolder = 3,  // `path`
                Rename = 4,      // `path` to `path2`; fails if `path2` exists
                Remove = 5,      // a file or link
                RemoveFolder = 6, // an empty folder
                Desktop = 7       // answered with `text`: the folder dropped files go to (the Desktop)
            };
            uint32_t id = 0; // never 0; the answer repeats it
            Op op = Op::Home;
            std::string path;
            std::string path2;

            Bytes Encode() const;
            static FileRequest Decode(Reader& reader);
        };

        struct FileResult
        {
            uint32_t id = 0;
            FileStatus status = FileStatus::Ok;
            std::string error; // when not Ok
            std::vector<FileInfo> entries;
            std::string text; // Home's folder

            Bytes Encode() const;
            static FileResult Decode(Reader& reader);
        };

        struct FileOpen
        {
            enum class Mode : uint8_t
            {
                Read = 0,
                Create = 1,    // a new file; fails if there is one
                Overwrite = 2, // replaces the file
                Resume = 3     // keeps the first `offset` bytes of the file and carries on from there
            };
            uint32_t id = 0;
            Mode mode = Mode::Read;
            std::string path;
            uint64_t offset = 0; // Resume only

            Bytes Encode() const;
            static FileOpen Decode(Reader& reader);
        };

        struct FileOpened
        {
            uint32_t id = 0;
            FileStatus status = FileStatus::Ok;
            std::string error;
            uint32_t handle = 0;
            uint64_t size = 0; // the file's size when it was opened

            Bytes Encode() const;
            static FileOpened Decode(Reader& reader);
        };

        struct FileRead
        {
            uint32_t id = 0;
            uint32_t handle = 0;
            uint64_t offset = 0;
            uint32_t length = 0; // at most kMaxFileChunk

            Bytes Encode() const;
            static FileRead Decode(Reader& reader);
        };

        struct FileData
        {
            uint32_t id = 0; // the FileRead's
            FileStatus status = FileStatus::Ok;
            std::string error;
            bool end = false; // the range reached the end of the file
            Bytes data;

            Bytes Encode() const;
            static FileData Decode(Reader& reader);
        };

        struct FileWrite
        {
            uint32_t handle = 0;
            uint64_t offset = 0; // must continue where the last write on this handle stopped
            Bytes data;

            Bytes Encode() const;
            static FileWrite Decode(Reader& reader);
        };

        struct FileClose
        {
            uint32_t id = 0; // 0: close without an answer
            uint32_t handle = 0;

            Bytes Encode() const;
            static FileClose Decode(Reader& reader);
        };

        struct FileWriteFailed
        {
            uint32_t handle = 0;
            FileStatus status = FileStatus::Failed;
            std::string error;

            Bytes Encode() const;
            static FileWriteFailed Decode(Reader& reader);
        };
    } // namespace Wire
} // namespace Marconi
