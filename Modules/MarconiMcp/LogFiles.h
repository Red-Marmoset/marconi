// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Where MarconiMcp writes its log: the viewer's log folder, with only the most recent few kept

#pragma once

#include <string>

namespace Marconi
{
    namespace Mcp
    {
        // The viewer's log folder, worked out the way wxWidgets does for it (MarconiMcp has no wx):
        // %LOCALAPPDATA%\marconi\logs on Windows, ~/Library/Application Support/marconi/logs on macOS, and
        // ~/.marconi/logs elsewhere. Empty if the home folder can't be found.
        std::string LogDirectory();

        // Makes the folder (private to this user), removes all but the newest `keep` earlier MarconiMcp logs, and
        // starts logging to a new file there. False if it couldn't; logging then goes to standard error only.
        bool StartFileLogging(int keep = 20);
    } // namespace Mcp
} // namespace Marconi
