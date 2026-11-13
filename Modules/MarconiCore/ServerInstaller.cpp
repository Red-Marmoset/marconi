// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finding the Marconi Server installer that travels with Marconi

#include "ServerInstaller.h"

#include <MarconiCore/Update.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace Marconi
{
    namespace ServerInstaller
    {
        namespace
        {
            namespace fs = std::filesystem;

            struct Candidate
            {
                std::string path;
                Update::Version version;
                bool exact = false;
            };

            // The version in "MarconiServer-<version>-win64-setup.exe", if the name has that shape.
            bool ParseName(const std::string& name, Update::Version& out)
            {
                const std::string prefix = kFilePrefix;
                const std::string suffix = kFileSuffix;
                if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
                {
                    return false;
                }
                return Update::Version::Parse(name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()),
                                              out);
            }

            void Collect(const fs::path& folder, const std::string& currentVersion, std::vector<Candidate>& out)
            {
                std::error_code ec;
                for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
                {
                    std::error_code fileError;
                    if (!it->is_regular_file(fileError))
                    {
                        continue;
                    }
                    Candidate candidate;
                    if (!ParseName(it->path().filename().string(), candidate.version))
                    {
                        continue;
                    }
                    candidate.path = it->path().generic_string();
                    candidate.exact = candidate.version.ToString() == currentVersion;
                    out.push_back(candidate);
                }
            }
        } // namespace

        std::string Find(const std::string& programFolder, const std::string& currentVersion)
        {
            if (programFolder.empty())
            {
                return std::string();
            }
            std::vector<Candidate> found;
            fs::path folder(programFolder);
            Collect(folder, currentVersion, found);
            if (found.empty())
            {
                // A development build: Build/Marconi/Release beside Build/Installer.
                Collect((folder / ".." / ".." / "Installer").lexically_normal(), currentVersion, found);
            }
            const Candidate* best = nullptr;
            for (const Candidate& candidate : found)
            {
                if (best == nullptr || (candidate.exact && !best->exact) ||
                    (candidate.exact == best->exact && candidate.version.Compare(best->version) > 0))
                {
                    best = &candidate;
                }
            }
            return best != nullptr ? best->path : std::string();
        }

        std::string VersionOfFile(const std::string& path)
        {
            Update::Version version;
            if (!ParseName(fs::path(path).filename().string(), version))
            {
                return std::string();
            }
            return version.ToString();
        }
    } // namespace ServerInstaller
} // namespace Marconi
