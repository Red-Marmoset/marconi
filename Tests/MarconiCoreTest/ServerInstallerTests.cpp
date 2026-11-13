// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for finding the Marconi Server installer that travels with the client

#include "TestSupport.h"

#include <MarconiCore/ServerInstaller.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace MarconiTest
{
    using namespace Marconi;
    namespace fs = std::filesystem;

    namespace
    {
        void Touch(const fs::path& file)
        {
            fs::create_directories(file.parent_path());
            std::ofstream(file) << "x";
        }
    } // namespace

    void RunServerInstallerTests()
    {
        RunTest("The server installer is found next to the program, preferring the current version", []() {
            std::random_device random;
            fs::path root = fs::temp_directory_path() / ("marconi_installer_test_" + std::to_string(random()));
            fs::path program = root / "Marconi";
            Touch(program / "Marconi.exe");
            CHECK(ServerInstaller::Find(program.generic_string(), "0.9.7.100").empty());

            Touch(program / "MarconiServer-0.9.6.90-win64-setup.exe");
            Touch(program / "MarconiServer-0.9.7.100-win64-setup.exe");
            Touch(program / "MarconiServer-0.9.8.5-win64-setup.exe");
            Touch(program / "MarconiServer-notes.txt");
            Touch(program / "Other-0.9.9.9-win64-setup.exe");
            // The one for this version, even though a newer one is there.
            CHECK(ServerInstaller::Find(program.generic_string(), "0.9.7.100") ==
                  (program / "MarconiServer-0.9.7.100-win64-setup.exe").generic_string());
            // None for this version: the newest.
            CHECK(ServerInstaller::Find(program.generic_string(), "0.9.7.101") ==
                  (program / "MarconiServer-0.9.8.5-win64-setup.exe").generic_string());
            CHECK(ServerInstaller::Find("", "0.9.7.100").empty());
            std::error_code ec;
            fs::remove_all(root, ec);
        });

        RunTest("A development build finds the installer in Build/Installer", []() {
            std::random_device random;
            fs::path root = fs::temp_directory_path() / ("marconi_installer_dev_" + std::to_string(random()));
            fs::path program = root / "Build" / "Marconi" / "Release";
            fs::create_directories(program);
            Touch(root / "Build" / "Installer" / "MarconiServer-0.9.7.100-win64-setup.exe");
            std::string found = ServerInstaller::Find(program.generic_string(), "0.9.7.100");
            CHECK(!found.empty());
            CHECK(fs::path(found).filename() == "MarconiServer-0.9.7.100-win64-setup.exe");
            std::error_code ec;
            fs::remove_all(root, ec);
        });
    }
} // namespace MarconiTest
