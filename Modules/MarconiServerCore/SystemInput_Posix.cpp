// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No input injection on macOS or Linux yet: the caller falls back to logging the input. (macOS CGEvent and Linux
// XTest come in later phases.)

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
