// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Removing everything Marconi keeps for the current user: saved connections, their passwords, settings and logs

#pragma once

#include <wx/string.h>

namespace MarconiUI
{
    // The folder Marconi's logs (and headless snapshots) go in.
    wxString GetLogDirectory();

    // Removes the saved connections and their keychain passwords, the appearance choice and every log file that can
    // go now. A log file Marconi has open can't be removed on Windows, so the folder itself is removed by
    // RemoveLogDirectory once logging has stopped; ShouldRemoveLogsOnExit says whether that is still owed.
    void RemoveAllUserData();
    bool ShouldRemoveLogsOnExit();
    void RemoveLogDirectory();
} // namespace MarconiUI
