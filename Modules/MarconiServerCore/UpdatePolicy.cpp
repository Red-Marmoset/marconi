// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The rules for letting a newer Marconi update this server

#include "UpdatePolicy.h"

#include <MarconiCore/ServerUpdate.h>

#include <Crypto/Crypto.h>

#include <fstream>

namespace Marconi
{
    namespace Server
    {
        const char* UpdatePermissionName(UpdatePermission permission)
        {
            switch (permission)
            {
            case UpdatePermission::Off:
                return "off";
            case UpdatePermission::Allow:
                return "allow";
            case UpdatePermission::Ask:
            default:
                return "ask";
            }
        }

        UpdatePermission ParseUpdatePermission(const std::string& text)
        {
            if (text == "off")
            {
                return UpdatePermission::Off;
            }
            if (text == "allow")
            {
                return UpdatePermission::Allow;
            }
            return UpdatePermission::Ask;
        }

        Wire::UpdateAnswer::Status DecideOffer(const Wire::UpdateOffer& offer, const OfferContext& context)
        {
            using Status = Wire::UpdateAnswer::Status;
            if (offer.product != Wire::kServerProductName)
            {
                return Status::WrongProduct;
            }
            if (!context.supported)
            {
                return Status::NotSupportedHere;
            }
            if (offer.platform != context.platform)
            {
                return Status::WrongProduct;
            }
            Update::Version parsed;
            if (!Update::Version::Parse(offer.version, parsed))
            {
                return Status::BadOffer;
            }
            if (!ServerUpdate::IsNewer(offer.version, context.runningVersion))
            {
                return Status::NotNewer; // a downgrade, or the same version again
            }
            if (context.permission == UpdatePermission::Off)
            {
                return Status::TurnedOff;
            }
            if (!context.runningSigned && !context.allowUnsigned)
            {
                return Status::CannotVerify;
            }
            const bool ask = context.permission == UpdatePermission::Ask;
            if (ask && !context.canAskPerson)
            {
                return Status::NotSupportedHere; // set to ask, but there is nobody to ask (no tray program)
            }
            if (offer.IsCheckOnly())
            {
                return ask ? Status::NeedsApproval : Status::WouldAccept;
            }
            if (offer.sha256.size() != 32 || offer.size == 0)
            {
                return Status::BadOffer;
            }
            if (offer.size > Wire::kMaxUpdateBytes)
            {
                return Status::TooLarge;
            }
            if (context.busy)
            {
                return Status::Busy;
            }
            if (context.freeBytes != UINT64_MAX && context.freeBytes < offer.size + kSpareBytes)
            {
                return Status::NoRoom;
            }
            return ask ? Status::AskingPerson : Status::SendIt;
        }

        bool SignatureAcceptable(const InstallerFacts& running, const InstallerFacts& candidate, bool allowUnsigned,
                                 std::string& why)
        {
            if (candidate.signature == SignatureState::Invalid)
            {
                why = "the installer's signature is broken or not trusted";
                return false;
            }
            if (running.signature == SignatureState::Valid)
            {
                if (candidate.signature != SignatureState::Valid)
                {
                    why = "this server is signed, and the installer isn't";
                    return false;
                }
                if (candidate.signer != running.signer)
                {
                    why = "the installer is signed by '" + candidate.signer + "', not '" + running.signer + "'";
                    return false;
                }
                return true;
            }
            if (candidate.signature == SignatureState::Valid)
            {
                if (candidate.signer == kExpectedPublisher)
                {
                    return true;
                }
                why = "the installer is signed by '" + candidate.signer + "', not '" + kExpectedPublisher + "'";
                return false;
            }
            if (allowUnsigned)
            {
                return true; // Missing or Unavailable, and unsigned updates are allowed
            }
            why = "the installer can't be checked (not signed) and unsigned updates aren't allowed";
            return false;
        }

        Wire::UpdateResult::Status CheckArrivedInstaller(const Wire::UpdateOffer& offer, uint64_t fileSize,
                                                         const Wire::Bytes& fileSha256, const InstallerFacts& running,
                                                         const InstallerFacts& candidate,
                                                         const std::string& runningVersion, bool allowUnsigned,
                                                         std::string& detail)
        {
            using Status = Wire::UpdateResult::Status;
            if (fileSize != offer.size)
            {
                detail = "the file is " + std::to_string(fileSize) + " bytes, not " + std::to_string(offer.size);
                return Status::BadUpload;
            }
            if (fileSha256.size() != 32 || fileSha256 != offer.sha256)
            {
                detail = "the file's SHA-256 isn't the one offered";
                return Status::HashMismatch;
            }
            if (candidate.product != Wire::kServerProductName)
            {
                detail = "the file says it is '" + candidate.product + "'";
                return Status::VersionMismatch;
            }
            if (!ServerUpdate::IsSameVersion(candidate.version, offer.version))
            {
                detail = "the file says it is version '" + candidate.version + "', the offer said " + offer.version;
                return Status::VersionMismatch;
            }
            if (!ServerUpdate::IsNewer(candidate.version, runningVersion))
            {
                detail = "version " + candidate.version + " isn't newer than " + runningVersion;
                return Status::NotNewer;
            }
            std::string why;
            if (!SignatureAcceptable(running, candidate, allowUnsigned, why))
            {
                detail = why;
                return Status::SignatureRefused;
            }
            return Status::Installing;
        }

        bool HashFile(const std::string& path, Wire::Bytes& sha256, uint64_t& size)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return false;
            }
            in.seekg(0, std::ios::end);
            std::streamoff length = in.tellg();
            if (length < 0 || static_cast<uint64_t>(length) > Wire::kMaxUpdateBytes)
            {
                return false;
            }
            in.seekg(0, std::ios::beg);
            Wire::Bytes data(static_cast<size_t>(length));
            if (length > 0 && !in.read(reinterpret_cast<char*>(data.data()), length))
            {
                return false;
            }
            size = data.size();
            sha256.assign(32, 0);
            Crypto::Sha256(data.data(), data.size(), sha256.data());
            return true;
        }
    } // namespace Server
} // namespace Marconi
