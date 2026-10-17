// Copyright (c) 2026 Red Marmoset Studios Ltd.
// How the viewer retries after a connection is lost: the waits between attempts, and when to stop

#pragma once

namespace Marconi
{
    namespace Reconnect
    {
        // Seconds to wait before attempt `attempt` (1 for the first): 2, 4, 8, 16, then 30 from there on.
        int DelaySeconds(int attempt);

        // Attempts stop (and wait for the user) once this long has passed since the connection was lost.
        constexpr int kGiveUpSeconds = 300;
        bool ShouldGiveUp(int secondsSinceLost);
    } // namespace Reconnect
} // namespace Marconi
