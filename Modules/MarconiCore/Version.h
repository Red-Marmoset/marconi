// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Marconi version information

#pragma once

namespace Marconi
{
    // MAJOR.MINOR.PATCH.REVISION, e.g. "0.1.0.27". MAJOR.MINOR.PATCH comes from Project.json and REVISION is the
    // number of commits in the build's history, both written to BuildVersion.h by Build.ps1 / Build.sh.
    const char* GetVersionString();

    // The abbreviated hash of the commit the build was made from, or "unknown" without git.
    const char* GetVersionCommit();
} // namespace Marconi
