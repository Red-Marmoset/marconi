// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading an access password from a file the installer left, so it never travels on a command line

#pragma once

#include <string>

namespace Marconi
{
    namespace Server
    {
        // Reads the password in the file at `path` and returns it as UTF-8. The file is UTF-16 little-endian with a
        // byte order mark (what the Windows installer writes) or plain UTF-8 (with or without a mark); one trailing
        // line break is dropped. False, with `error`, if the file can't be read, isn't valid text or holds nothing.
        bool ReadPasswordFile(const std::string& path, std::string& passwordOut, std::string& error);

        // Overwrites the file with zeros, then deletes it (the password's plain text shouldn't linger on disk). False
        // if it couldn't be removed.
        bool ErasePasswordFile(const std::string& path);
    } // namespace Server
} // namespace Marconi
