// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finding out whether a newer Marconi has been released.

#include <MarconiCore/Update.h>

#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <json/single_include/nlohmann/json.hpp>

#include <cstdlib>

namespace Marconi
{
    namespace Update
    {
        bool Version::Parse(const std::string& text, Version& out)
        {
            out = Version();
            size_t p = 0;
            if (p < text.size() && (text[p] == 'v' || text[p] == 'V'))
            {
                ++p;
            }
            int count = 0;
            while (count < 4)
            {
                if (p >= text.size() || !isdigit(static_cast<unsigned char>(text[p])))
                {
                    return false;
                }
                long value = 0;
                while (p < text.size() && isdigit(static_cast<unsigned char>(text[p])))
                {
                    value = value * 10 + (text[p] - '0');
                    if (value > 1000000)
                    {
                        return false;
                    }
                    ++p;
                }
                out.parts[count++] = static_cast<int>(value);
                if (p == text.size())
                {
                    break;
                }
                if (text[p] != '.')
                {
                    return false;
                }
                ++p;
            }
            return count >= 3 && p == text.size();
        }

        int Version::Compare(const Version& other) const
        {
            for (int i = 0; i < 4; ++i)
            {
                if (parts[i] != other.parts[i])
                {
                    return parts[i] < other.parts[i] ? -1 : 1;
                }
            }
            return 0;
        }

        std::string Version::ToString() const
        {
            return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." +
                   std::to_string(parts[3]);
        }

        std::string AssetSuffixForThisBuild()
        {
#if defined(_WIN32)
            return "-win64-setup.exe";
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
            return "-macos-arm64.dmg";
#else
            return "-macos-x86_64.dmg";
#endif
#else
            return "";
#endif
        }

        bool ParseReleases(const std::string& json, const std::string& assetSuffix, Release& out, std::string& error)
        {
            nlohmann::json releases = nlohmann::json::parse(json, nullptr, false);
            if (!releases.is_array())
            {
                error = "the releases list from GitHub wasn't in the expected form";
                return false;
            }
            bool found = false;
            for (const nlohmann::json& item : releases)
            {
                if (!item.is_object() || item.value("draft", false))
                {
                    continue;
                }
                Release release;
                release.tag = item.value("tag_name", "");
                if (!Version::Parse(release.tag, release.version))
                {
                    continue; // a tag that isn't a version: not one of Marconi's releases
                }
                if (found && release.version.Compare(out.version) <= 0)
                {
                    continue;
                }
                release.name = item.value("name", "");
                release.pageUrl = item.value("html_url", "");
                release.notes = item.value("body", "");
                release.prerelease = item.value("prerelease", false);
                const nlohmann::json& assets = item.contains("assets") ? item["assets"] : nlohmann::json::array();
                if (assets.is_array() && !assetSuffix.empty())
                {
                    for (const nlohmann::json& asset : assets)
                    {
                        std::string name = asset.is_object() ? asset.value("name", "") : "";
                        if (name.size() >= assetSuffix.size() &&
                            name.compare(name.size() - assetSuffix.size(), assetSuffix.size(), assetSuffix) == 0)
                        {
                            release.downloadUrl = asset.value("browser_download_url", "");
                            break;
                        }
                    }
                }
                out = release;
                found = true;
            }
            if (!found)
            {
                error = "GitHub lists no release with a version number";
                return false;
            }
            return true;
        }

        CheckResult CheckForUpdate(const std::string& currentVersion, Net::HttpsRequest& request, int timeoutMs,
                                   const std::string& host)
        {
            CheckResult result;
            Version current;
            if (!Version::Parse(currentVersion, current))
            {
                result.error = "this build's version (" + currentVersion + ") isn't a version number";
                return result;
            }
            std::map<std::string, std::string> headers;
            headers["User-Agent"] = "Marconi/" + currentVersion;
            headers["Accept"] = "application/vnd.github+json";
            headers["X-GitHub-Api-Version"] = "2022-11-28";
            Net::HttpResponse response;
            std::string error;
            if (!request.Get(host, kReleasesPath, headers, timeoutMs, response, error))
            {
                result.error = "Couldn't reach GitHub: " + error;
                return result;
            }
            if (response.status != 200)
            {
                // GitHub explains refusals in a "message" field (a rate limit, most often).
                result.error = "GitHub answered with status " + std::to_string(response.status);
                nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
                if (body.is_object() && body.contains("message") && body["message"].is_string())
                {
                    result.error += ": " + body["message"].get<std::string>();
                }
                return result;
            }
            if (!ParseReleases(response.body, AssetSuffixForThisBuild(), result.release, error))
            {
                result.error = error;
                return result;
            }
            result.checked = true;
            result.newer = result.release.version.Compare(current) > 0;
            LOG_INFO("Update", "Newest release on GitHub: %s (this is %s): %s", result.release.tag.c_str(),
                     currentVersion.c_str(), result.newer ? "newer" : "not newer");
            return result;
        }
    } // namespace Update
} // namespace Marconi
