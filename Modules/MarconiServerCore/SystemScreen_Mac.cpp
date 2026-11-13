// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No screen capture on macOS yet: the caller falls back to the test pattern. (macOS ScreenCaptureKit comes in a
// later phase; Linux is in SystemScreen_Linux.cpp.)

#include "SystemScreen.h"

namespace Marconi
{
    namespace Server
    {
        std::unique_ptr<ScreenSource> CreateSystemScreen(std::string& error)
        {
            error = "screen capture isn't available on this platform yet";
            return nullptr;
        }
    } // namespace Server
} // namespace Marconi
