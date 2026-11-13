// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The rules for letting a newer Marconi update this server: whether to take an offer, and whether the file that
// arrived may be run. Pure decisions over plain facts, so every case is tested on any system; the facts about a file
// (its signature, the product and version written in it) come from an InstallerInspector (UpdateHost.h).
// SERVER_PLAN.md, "Self-update", has the reasons and the threat model.

#pragma once

#include <MarconiCore/Wire/WireUpdateMessages.h>

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // The tray setting "Let a newer Marconi update this server".
        enum class UpdatePermission
        {
            Off,   // never: every offer is refused
            Ask,   // ask the person at this computer each time (the default); the viewer's input is held meanwhile
            Allow, // without asking (for computers nobody sits at); the installer's signature is still checked
        };

        // The settings file's words for it ("off", "ask", "allow"), and back. Anything unreadable is Ask.
        const char* UpdatePermissionName(UpdatePermission permission);
        UpdatePermission ParseUpdatePermission(const std::string& text);

        // The publisher name an installer's signature must carry when the running server can't be compared (it isn't
        // signed itself). The owner must confirm it matches the code-signing certificate (SERVER_PLAN.md).
        constexpr const char* kExpectedPublisher = "Red Marmoset Studios Ltd.";

        // How long the person at this computer has to answer before the update counts as turned down.
        constexpr int kApprovalSeconds = 120;

        enum class SignatureState
        {
            Valid,       // signed, the chain is trusted and the file is unchanged since signing
            Missing,     // not signed at all
            Invalid,     // signed, but the signature is broken, expired without a timestamp, untrusted or revoked
            Unavailable, // this system can't check signatures (or the check couldn't run)
        };

        // What could be learnt about an installer file, or about the running server's own program.
        struct InstallerFacts
        {
            SignatureState signature = SignatureState::Unavailable;
            std::string signer;  // the signing certificate's subject name, when signed
            std::string product; // the product name written in the file (its version information)
            std::string version; // the product version written in the file
        };

        // What the decision about an offer depends on, besides the offer.
        struct OfferContext
        {
            std::string runningVersion;
            std::string platform = Wire::kUpdatePlatformWindows; // what this server runs on
            UpdatePermission permission = UpdatePermission::Ask;
            bool allowUnsigned = false;
            bool supported = false;    // this server can start an installer (Windows, run by the tray program)
            bool canAskPerson = false; // there is a way to ask the person at this computer
            bool runningSigned = false; // the running server's own program has a valid signature
            bool busy = false;          // another update is under way
            uint64_t freeBytes = UINT64_MAX; // free space where the installer would be saved; UINT64_MAX = unknown
        };

        // Room kept free on top of the installer itself, so taking it can't fill the disk.
        constexpr uint64_t kSpareBytes = 32ull * 1024 * 1024;

        // The answer to an offer. For a real offer, SendIt means "take it now" and AskingPerson "ask first".
        Wire::UpdateAnswer::Status DecideOffer(const Wire::UpdateOffer& offer, const OfferContext& context);

        // Whether a candidate installer's signature is acceptable, given the running server's own. False with `why`
        // (for the log). The rules: a broken signature is always refused; when this server is signed, the installer
        // must be validly signed by the same signer; when it isn't, the installer must be validly signed by
        // kExpectedPublisher, or unsigned (or uncheckable) with `allowUnsigned` on.
        bool SignatureAcceptable(const InstallerFacts& running, const InstallerFacts& candidate, bool allowUnsigned,
                                 std::string& why);

        // Every check on the file that arrived, in order: its size, its SHA-256 against the offer's, the product and
        // version written in it against the offer's, strictly newer than the running server, its signature.
        Wire::UpdateResult::Status CheckArrivedInstaller(const Wire::UpdateOffer& offer, uint64_t fileSize,
                                                         const Wire::Bytes& fileSha256, const InstallerFacts& running,
                                                         const InstallerFacts& candidate,
                                                         const std::string& runningVersion, bool allowUnsigned,
                                                         std::string& detail);

        // The SHA-256 and size of a file on disk (at most Wire::kMaxUpdateBytes). False if it can't be read.
        bool HashFile(const std::string& path, Wire::Bytes& sha256, uint64_t& size);
    } // namespace Server
} // namespace Marconi
