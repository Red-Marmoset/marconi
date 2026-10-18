// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Makes the real screen source for this computer, when there is one for this platform

#pragma once

#include "ScreenSource.h"

#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // The platform's screen capture: Windows uses the desktop's own drawing (GDI) for now. Returns nullptr
        // (with `error`) on a platform without capture yet, so the caller falls back to the test pattern.
        std::unique_ptr<ScreenSource> CreateSystemScreen(std::string& error);
    } // namespace Server
} // namespace Marconi
