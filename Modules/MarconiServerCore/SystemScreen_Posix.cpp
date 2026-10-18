// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No screen capture on macOS or Linux yet: the caller falls back to the test pattern. (macOS ScreenCaptureKit
// and Linux PipeWire/X11 come in later phases.)

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
