// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's side of updating an older Marconi Server: whether to offer it, and what to tell the person, in plain
// words. No wx headers, so the decisions are tested on any system (SERVER_PLAN.md, "Self-update").

#pragma once

#include <MarconiCore/Update.h>
#include <MarconiCore/Wire/WireUpdateMessages.h>

#include <string>

namespace Marconi
{
    namespace ServerUpdate
    {
        // Whether to offer an update when connected to a Marconi Server, and if not, why not.
        enum class OfferDecision
        {
            Offer,            // the installer here is newer than the server and the server can take it
            UpToDate,         // the server is as new as the installer here, or newer
            ServerTooOld,     // the server predates updating from a viewer: it must be updated by hand once
            NoInstaller,      // this Marconi has no Marconi Server installer with it
            UnknownVersion,   // the server didn't say its version in a form Marconi can compare
            DontAsk,          // the person said not to ask again for this computer (on connect only)
        };

        struct OfferInputs
        {
            std::string serverVersion;      // from the server's Welcome
            bool serverKnowsUpdates = false; // the server granted Capability::ServerUpdate
            std::string installerPath;      // ServerInstaller::Find's answer (empty = none)
            bool dontAsk = false;           // the saved connection says "don't ask again" (ignored for the menu item)
            bool fromMenu = false;          // the person chose the menu item, so "don't ask again" doesn't apply
        };

        OfferDecision DecideOffer(const OfferInputs& inputs);

        // The installer's version from its file name ("0.10.0.512"), or empty.
        std::string InstallerVersion(const std::string& installerPath);

        // True if `newer` is strictly newer than `older` (both MAJOR.MINOR.PATCH[.REVISION]); false if either can't
        // be read. The server uses the same rule to refuse downgrades and reinstalls.
        bool IsNewer(const std::string& newer, const std::string& older);
        // True if both are readable and the same version.
        bool IsSameVersion(const std::string& a, const std::string& b);

        // After the update the viewer reconnects and compares: did the server come back as the new version?
        enum class Outcome
        {
            Updated,  // it runs the version that was sent
            StillOld, // it came back, but older than what was sent: the installer didn't finish
            Other,    // it runs some other version (someone else updated it meanwhile)
        };
        Outcome CheckAfterReconnect(const std::string& sentVersion, const std::string& serverVersionNow);

        // Plain-words sentences for the person using Marconi. `computer` is the other computer's name as the window
        // shows it.
        std::string DescribeDecision(OfferDecision decision, const std::string& computer);
        std::string DescribeAnswer(Wire::UpdateAnswer::Status status, const std::string& computer);
        std::string DescribeResult(Wire::UpdateResult::Status status, const std::string& computer);
        std::string DescribeOutcome(Outcome outcome, const std::string& computer, const std::string& sentVersion,
                                    const std::string& serverVersionNow);
        // The question asked on connecting: "Marconi Server on <computer> is older (0.9.7) than ..."
        std::string OfferQuestion(const std::string& computer, const std::string& serverVersion,
                                  const std::string& installerVersion);
    } // namespace ServerUpdate
} // namespace Marconi
