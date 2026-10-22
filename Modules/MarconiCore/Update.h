// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finding out whether a newer Marconi has been released: GitHub's releases list, read and compared with this build

#pragma once

#include <MarconiCore/Net/Https.h>

#include <string>

namespace Marconi
{
    namespace Update
    {
        // MAJOR.MINOR.PATCH.REVISION, as Marconi's versions and release tags are written ("v" allowed in front).
        struct Version
        {
            int parts[4] = {0, 0, 0, 0};

            static bool Parse(const std::string& text, Version& out);
            int Compare(const Version& other) const; // <0, 0, >0
            std::string ToString() const;
        };

        struct Release
        {
            std::string tag;         // e.g. "v0.6.0.169"
            Version version;
            std::string name;        // e.g. "Marconi 0.6.0.169"
            std::string pageUrl;     // the release page, for people
            std::string downloadUrl; // the installer or disk image for this computer, if the release has one
            std::string notes;       // the release's description
            bool prerelease = false;
        };

        // The ending of the release file made for this build: "-win64-setup.exe", "-macos-arm64.dmg", ... or ""
        // on a system with no packaged build.
        std::string AssetSuffixForThisBuild();

        // Picks the newest published release (drafts are skipped; pre-releases count, since every Marconi release
        // is one while it is 0.x) from GitHub's releases JSON. `assetSuffix` chooses the download. False with
        // `error` if the text isn't a releases list, or holds no release with a readable version.
        bool ParseReleases(const std::string& json, const std::string& assetSuffix, Release& out, std::string& error);

        struct CheckResult
        {
            bool checked = false; // the releases list was fetched and read
            bool newer = false;   // `release` is newer than the running version
            Release release;      // the newest release, when checked
            std::string error;    // why not, when !checked
        };

        constexpr const char* kReleasesHost = "api.github.com";
        constexpr const char* kReleasesPath = "/repos/Red-Marmoset/marconi/releases?per_page=10";

        // Asks GitHub for the releases and compares the newest with `currentVersion`. Blocks (a worker thread's
        // job); `request` can be cancelled from another thread. `host` is GitHub's API; tests point it elsewhere.
        CheckResult CheckForUpdate(const std::string& currentVersion, Net::HttpsRequest& request, int timeoutMs,
                                   const std::string& host = kReleasesHost);
    } // namespace Update
} // namespace Marconi
