// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer's side of updating an older Marconi Server

#include <MarconiCore/ServerUpdate.h>

#include <MarconiCore/ServerInstaller.h>

namespace Marconi
{
    namespace ServerUpdate
    {
        bool IsNewer(const std::string& newer, const std::string& older)
        {
            Update::Version a;
            Update::Version b;
            return Update::Version::Parse(newer, a) && Update::Version::Parse(older, b) && a.Compare(b) > 0;
        }

        bool IsSameVersion(const std::string& a, const std::string& b)
        {
            Update::Version x;
            Update::Version y;
            return Update::Version::Parse(a, x) && Update::Version::Parse(b, y) && x.Compare(y) == 0;
        }

        std::string InstallerVersion(const std::string& installerPath)
        {
            return installerPath.empty() ? std::string() : ServerInstaller::VersionOfFile(installerPath);
        }

        OfferDecision DecideOffer(const OfferInputs& inputs)
        {
            Update::Version server;
            if (!Update::Version::Parse(inputs.serverVersion, server))
            {
                return OfferDecision::UnknownVersion;
            }
            const std::string installer = InstallerVersion(inputs.installerPath);
            if (installer.empty())
            {
                return OfferDecision::NoInstaller;
            }
            if (!IsNewer(installer, inputs.serverVersion))
            {
                return OfferDecision::UpToDate;
            }
            if (!inputs.serverKnowsUpdates)
            {
                return OfferDecision::ServerTooOld;
            }
            if (inputs.dontAsk && !inputs.fromMenu)
            {
                return OfferDecision::DontAsk;
            }
            return OfferDecision::Offer;
        }

        Outcome CheckAfterReconnect(const std::string& sentVersion, const std::string& serverVersionNow)
        {
            if (IsSameVersion(sentVersion, serverVersionNow))
            {
                return Outcome::Updated;
            }
            if (IsNewer(sentVersion, serverVersionNow))
            {
                return Outcome::StillOld;
            }
            return Outcome::Other;
        }

        std::string DescribeDecision(OfferDecision decision, const std::string& computer)
        {
            switch (decision)
            {
            case OfferDecision::Offer:
                return "Marconi can update Marconi Server on " + computer + ".";
            case OfferDecision::UpToDate:
                return "Marconi Server on " + computer +
                       " is already as new as the one that came with this Marconi, so there is nothing to update.";
            case OfferDecision::ServerTooOld:
                return "Marconi Server on " + computer +
                       " is too old to be updated from here. Install the new version on it once by hand (Install "
                       "Marconi Server on the Other Computer); after that, Marconi can update it for you.";
            case OfferDecision::NoInstaller:
                return "This copy of Marconi doesn't have the Marconi Server installer with it, so it can't update "
                       "Marconi Server on " + computer + ".";
            case OfferDecision::UnknownVersion:
                return "Marconi Server on " + computer + " didn't say which version it is, so Marconi can't tell "
                       "whether it needs updating.";
            case OfferDecision::DontAsk:
            default:
                return "You asked Marconi not to offer updates for " + computer + ".";
            }
        }

        std::string DescribeAnswer(Wire::UpdateAnswer::Status status, const std::string& computer)
        {
            using Status = Wire::UpdateAnswer::Status;
            switch (status)
            {
            case Status::SendIt:
                return "Sending the update to " + computer + "...";
            case Status::WouldAccept:
                return "Marconi Server on " + computer + " can be updated from here.";
            case Status::NeedsApproval:
                return "Marconi Server on " + computer +
                       " can be updated from here, once someone at that computer allows it.";
            case Status::AskingPerson:
                return "Waiting for someone at " + computer + " to allow the update. Until they answer (or two "
                       "minutes pass), your mouse and keyboard don't reach that computer.";
            case Status::NotNewer:
                return "Marconi Server on " + computer + " is already as new as this update, so nothing was changed.";
            case Status::WrongProduct:
                return "Marconi Server on " + computer + " runs on a system this update isn't for.";
            case Status::NotSupportedHere:
                return "Marconi Server on " + computer +
                       " can't update itself on that system. Someone there needs to install the new version.";
            case Status::TurnedOff:
                return "Marconi Server on " + computer + " is set not to let other computers update it. Someone "
                       "there can change that from its icon (Let a newer Marconi update this server).";
            case Status::Declined:
                return "The update was turned down at " + computer + " (or nobody answered in time). Nothing was "
                       "changed.";
            case Status::TooLarge:
                return "The update is larger than Marconi Server on " + computer + " accepts.";
            case Status::Busy:
                return "Marconi Server on " + computer + " is already being updated.";
            case Status::NoRoom:
                return computer + " doesn't have enough free disk space for the update.";
            case Status::CannotVerify:
                return "Marconi Server on " + computer +
                       " can't check where updates come from, because it isn't signed itself, and it is set to take "
                       "only signed updates. Someone there can allow unsigned updates from its icon, or install the "
                       "new version by hand.";
            case Status::BadOffer:
            default:
                return "Marconi Server on " + computer + " couldn't read the update offer.";
            }
        }

        std::string DescribeResult(Wire::UpdateResult::Status status, const std::string& computer)
        {
            using Status = Wire::UpdateResult::Status;
            switch (status)
            {
            case Status::Installing:
                return "Marconi Server on " + computer +
                       " is updating. Marconi reconnects by itself in a moment. If Windows asks for permission on "
                       "that computer, say yes there.";
            case Status::HashMismatch:
                return "The update arrived damaged on " + computer + ", so it was not used. Nothing was changed.";
            case Status::SignatureRefused:
                return "Marconi Server on " + computer + " refused the update because it isn't signed by the same "
                       "publisher as the version it runs. Nothing was changed.";
            case Status::WriteFailed:
                return computer + " couldn't save the update. Nothing was changed.";
            case Status::NoRoom:
                return computer + " ran out of disk space while saving the update. Nothing was changed.";
            case Status::LaunchFailed:
                return "Marconi Server on " + computer + " couldn't start the update (or permission to run it was "
                       "refused there). Nothing was changed.";
            case Status::BadUpload:
                return "The update didn't arrive properly on " + computer + ". Nothing was changed; try again.";
            case Status::VersionMismatch:
                return "The update file isn't the Marconi Server version it claimed to be, so " + computer +
                       " refused it. Nothing was changed.";
            case Status::NotNewer:
            default:
                return "The update file isn't newer than Marconi Server on " + computer +
                       ", so it was refused. Nothing was changed.";
            }
        }

        std::string DescribeOutcome(Outcome outcome, const std::string& computer, const std::string& sentVersion,
                                    const std::string& serverVersionNow)
        {
            switch (outcome)
            {
            case Outcome::Updated:
                return "Marconi Server on " + computer + " is now version " + serverVersionNow + ".";
            case Outcome::StillOld:
                return "The update didn't finish: Marconi Server on " + computer + " is still version " +
                       serverVersionNow + " (the update was " + sentVersion +
                       "). The previous version keeps working. Someone at that computer can run the installer by "
                       "hand.";
            case Outcome::Other:
            default:
                return "Marconi Server on " + computer + " came back as version " + serverVersionNow +
                       ", not the " + sentVersion + " that was sent.";
            }
        }

        std::string OfferQuestion(const std::string& computer, const std::string& serverVersion,
                                  const std::string& installerVersion)
        {
            return "Marconi Server on " + computer + " is older (" + serverVersion +
                   ") than the one that came with this Marconi (" + installerVersion + "). Update it now?";
        }
    } // namespace ServerUpdate
} // namespace Marconi
