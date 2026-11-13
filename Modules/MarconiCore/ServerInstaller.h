// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Finding the Marconi Server installer that travels with Marconi, to hand it to another computer or run it here

#pragma once

#include <string>

namespace Marconi
{
    namespace ServerInstaller
    {
        // Where the latest releases are listed, for the case where the installer can't be sent as a file.
        constexpr const char* kDownloadPage = "https://github.com/Red-Marmoset/marconi/releases/latest";

        // The ending of the installer's file name: "MarconiServer-<version>-win64-setup.exe".
        constexpr const char* kFilePrefix = "MarconiServer-";
        constexpr const char* kFileSuffix = "-win64-setup.exe";

        // Looks for the installer next to the running program (`programFolder`: where the installer put it) and, for
        // a development build, in the build's Installer folder, two folders up from where the program is built
        // (Build/Marconi/Release -> Build/Installer). When several are there, the one for `currentVersion` wins,
        // else the newest. Returns its full path with '/' separators, or an empty string if there is none.
        std::string Find(const std::string& programFolder, const std::string& currentVersion);

        // The version in an installer's file name or path ("MarconiServer-0.10.0.512-win64-setup.exe"), as
        // MAJOR.MINOR.PATCH.REVISION text. Empty if the name doesn't have that shape.
        std::string VersionOfFile(const std::string& path);
    } // namespace ServerInstaller
} // namespace Marconi
