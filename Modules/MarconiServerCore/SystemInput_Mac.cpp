// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No input injection on macOS yet: the caller falls back to logging the input. (macOS CGEvent comes in a later
// phase; Linux is in SystemInput_Linux.cpp.)

#include "SystemInput.h"

namespace Marconi
{
    namespace Server
    {
        std::unique_ptr<InputSink> CreateSystemInput()
        {
            return nullptr;
        }
    } // namespace Server
} // namespace Marconi
