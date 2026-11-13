// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No clipboard sharing on macOS yet: the server runs without it. (Linux is in ClipboardAccess_Linux.cpp, Windows in
// ClipboardAccess_Windows.cpp.)

#include "ClipboardAccess.h"

namespace Marconi
{
    namespace Server
    {
        std::unique_ptr<ClipboardAccess> CreateSystemClipboard()
        {
            return nullptr;
        }
    } // namespace Server
} // namespace Marconi
