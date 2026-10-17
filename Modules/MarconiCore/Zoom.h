// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The zoom levels the viewer offers besides fitting the window, and stepping between them

#pragma once

#include <cstddef>

namespace Marconi
{
    namespace Zoom
    {
        constexpr int kLevels[] = {50, 75, 100, 125, 150, 200};
        constexpr size_t kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);

        // The level above (`in`) or below the given percentage, which needn't be a level itself (a Fit percentage
        // between two levels steps to the neighbouring one). Stays at the first or last level beyond the ends.
        int Next(int currentPercent, bool in);

        bool IsLevel(int percent);
    } // namespace Zoom
} // namespace Marconi
