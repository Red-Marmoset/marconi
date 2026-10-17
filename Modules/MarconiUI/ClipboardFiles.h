// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Files on the local clipboard, as Explorer and Finder put them there with Copy and take them with Paste

#pragma once

#include <string>
#include <vector>

namespace MarconiUI
{
    // Whether the clipboard holds a list of files (not text or a picture). UI thread.
    bool ClipboardHasFiles();

    // The files on the clipboard, as UTF-8 paths with '/' separators; empty if there are none. UI thread.
    std::vector<std::string> GetClipboardFiles();

    // Puts these files on the clipboard, for pasting in Explorer or Finder (or Marconi). UI thread.
    bool SetClipboardFiles(const std::vector<std::string>& paths);
} // namespace MarconiUI
