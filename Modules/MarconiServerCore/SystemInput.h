// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Makes the real input sink for this computer, when there is one for this platform

#pragma once

#include "InputSink.h"

#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        // The platform's input injection (SendInput on Windows, XTest on Linux, CGEvent on macOS). Returns nullptr
        // on a platform without it, or when the system won't allow it (on a Mac, until Accessibility is allowed), so
        // the caller falls back to logging the input instead of applying it. `error` then says why, in plain words
        // that can be shown to the person at the computer.
        std::unique_ptr<InputSink> CreateSystemInput(std::string& error);

        // The same without the reason.
        inline std::unique_ptr<InputSink> CreateSystemInput()
        {
            std::string error;
            return CreateSystemInput(error);
        }
    } // namespace Server
} // namespace Marconi
