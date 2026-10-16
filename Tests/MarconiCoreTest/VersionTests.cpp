// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the generated version information

#include "TestSupport.h"

#include <MarconiCore/BuildVersion.h>
#include <MarconiCore/Version.h>

#include <string>

namespace MarconiTest
{
    void RunVersionTests()
    {
        RunTest("Version string", []() {
            // MAJOR.MINOR.PATCH.REVISION, matching the numbers Marconi.rc puts in the Windows version resource.
            const std::string expected = std::to_string(MARCONI_VERSION_MAJOR) + "." +
                                         std::to_string(MARCONI_VERSION_MINOR) + "." +
                                         std::to_string(MARCONI_VERSION_PATCH) + "." +
                                         std::to_string(MARCONI_VERSION_REVISION);
            CHECK(expected == Marconi::GetVersionString());
            // VERSIONINFO stores each part in 16 bits.
            CHECK(MARCONI_VERSION_MAJOR <= 0xFFFF && MARCONI_VERSION_MINOR <= 0xFFFF);
            CHECK(MARCONI_VERSION_PATCH <= 0xFFFF && MARCONI_VERSION_REVISION <= 0xFFFF);
            CHECK(Marconi::GetVersionCommit()[0] != '\0');
        });
    }
} // namespace MarconiTest
