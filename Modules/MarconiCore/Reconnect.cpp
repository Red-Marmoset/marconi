// Copyright (c) 2026 Red Marmoset Studios Ltd.
// How the viewer retries after a connection is lost: the waits between attempts, and when to stop

#include <MarconiCore/Reconnect.h>

namespace Marconi
{
    namespace Reconnect
    {
        int DelaySeconds(int attempt)
        {
            switch (attempt)
            {
                case 1: return 2;
                case 2: return 4;
                case 3: return 8;
                case 4: return 16;
                default: return attempt < 1 ? 2 : 30;
            }
        }

        bool ShouldGiveUp(int secondsSinceLost)
        {
            return secondsSinceLost >= kGiveUpSeconds;
        }
    } // namespace Reconnect
} // namespace Marconi
