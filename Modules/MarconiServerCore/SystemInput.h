// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Makes the real input sink for this computer, when there is one for this platform

#pragma once

#include "InputSink.h"

#include <memory>

namespace Marconi
{
    namespace Server
    {
        // The platform's input injection (SendInput on Windows). Returns nullptr on a platform without it yet, so
        // the caller falls back to logging the input instead of applying it.
        std::unique_ptr<InputSink> CreateSystemInput();
    } // namespace Server
} // namespace Marconi
