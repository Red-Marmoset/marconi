// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No screen lock on macOS yet (the macOS server itself is Phase 8)

#include "SystemLock.h"

namespace Marconi
{
    namespace Server
    {
        bool LockThisComputer()
        {
            return false;
        }
    } // namespace Server
} // namespace Marconi
