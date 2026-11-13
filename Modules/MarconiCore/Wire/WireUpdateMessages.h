// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The messages a newer Marconi uses to update an older Marconi Server: offer, answer, the installer's pieces, result
//
// The design and the threat model are in SERVER_PLAN.md, "Self-update". In short: the viewer offers an installer
// (product, version, size, SHA-256); the server decides from its own settings whether to take it, may ask the person
// at its screen first, takes the file into a folder of its own choosing, checks the hash, the version and product
// written in the file and its signature, and only then starts it. Numbers 106 to 109 belong to these messages.

#pragma once

#include <MarconiCore/Wire/WireMessages.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Wire
    {
        // The product an update must be for (the installer's own product name says the same).
        constexpr const char* kServerProductName = "Marconi Server";
        // What a Windows server installer is for; a server on another system answers NotSupportedHere.
        constexpr const char* kUpdatePlatformWindows = "windows-x64";
        // The largest installer a server takes. The real one is a few megabytes.
        constexpr uint64_t kMaxUpdateBytes = 64ull * 1024 * 1024;
        // The size of each UpdateChunk the viewer sends (the last may be shorter).
        constexpr size_t kUpdateChunkBytes = 64 * 1024;

        // client -> server. With CheckOnly, only asks what the server would say to an update to `version`: nothing is
        // sent after it, nobody is asked, and size and hash may be empty. Otherwise it offers this installer.
        struct UpdateOffer
        {
            static constexpr uint8_t kCheckOnly = 1u << 0;
            uint8_t flags = 0;
            std::string product = kServerProductName;
            std::string version;  // MAJOR.MINOR.PATCH.REVISION of the installer
            std::string platform = kUpdatePlatformWindows;
            uint64_t size = 0;    // bytes in the installer
            Bytes sha256;         // its SHA-256, 32 bytes (empty for CheckOnly)

            bool IsCheckOnly() const { return (flags & kCheckOnly) != 0; }
            Bytes Encode() const;
            static UpdateOffer Decode(Reader& reader);
        };

        // server -> client, the answer to an UpdateOffer.
        struct UpdateAnswer
        {
            enum class Status : uint8_t
            {
                SendIt = 0,           // start sending UpdateChunks
                WouldAccept = 1,      // CheckOnly: an update would be taken without asking anyone
                NeedsApproval = 2,    // CheckOnly: someone at the server's screen would have to allow it
                AskingPerson = 3,     // the person at the server's screen is being asked; SendIt or Declined follows
                NotNewer = 4,         // the installer isn't newer than the running server
                WrongProduct = 5,     // not a Marconi Server installer, or not for this system
                NotSupportedHere = 6, // this server can't update itself (its system, or how it runs)
                TurnedOff = 7,        // the server's settings don't let viewers update it
                Declined = 8,         // the person at the server's screen said no, or didn't answer in time
                TooLarge = 9,         // bigger than kMaxUpdateBytes
                Busy = 10,            // another update is already under way
                NoRoom = 11,          // not enough free disk space for the installer
                CannotVerify = 12,    // the running server isn't signed, and unsigned updates aren't allowed
                BadOffer = 13,        // the offer is malformed (no hash, unreadable version)
            };
            Status status = Status::SendIt;
            std::string serverVersion; // the running server's version
            std::string detail;        // more for the log (never needed to understand the status)

            Bytes Encode() const;
            static UpdateAnswer Decode(Reader& reader);
        };

        // client -> server, one piece of the installer, in order: `offset` must be what has arrived so far.
        struct UpdateChunk
        {
            uint64_t offset = 0;
            Bytes data;

            Bytes Encode() const;
            static UpdateChunk Decode(Reader& reader);
        };

        // server -> client, once the whole file arrived (or receiving it failed).
        struct UpdateResult
        {
            enum class Status : uint8_t
            {
                Installing = 0,       // every check passed; the installer was started and this server is stopping
                HashMismatch = 1,     // the file isn't the one offered
                SignatureRefused = 2, // its signature is missing, broken or from another publisher
                WriteFailed = 3,      // the server couldn't save it
                NoRoom = 4,           // the disk filled up while saving it
                LaunchFailed = 5,     // the installer couldn't be started (or permission to run it was refused)
                BadUpload = 6,        // a piece arrived out of order, or more than was offered
                VersionMismatch = 7,  // the version or product written in the file isn't what was offered
                NotNewer = 8,         // the file's own version isn't newer than the running server
            };
            Status status = Status::Installing;
            std::string detail;

            Bytes Encode() const;
            static UpdateResult Decode(Reader& reader);
        };
    } // namespace Wire
} // namespace Marconi
