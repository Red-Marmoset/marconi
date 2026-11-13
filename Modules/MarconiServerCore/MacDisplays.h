// Copyright (c) 2026 Red Marmoset Studios Ltd.
// macOS only: the Mac's displays and where they sit, shared by the screen capture and the input injection.
//
// NOTE: written without a Mac to build on; not yet compiled or run on macOS (see SERVER_PLAN.md, Phase 8).
//
// Coordinates: macOS places every display in one "global" space measured in points (not pixels), origin at the top
// left of the main display, y growing downwards; displays to the left of or above the main one have negative
// coordinates. The server serves the rectangle that holds all displays, moved so its top left is (0,0) (the same as
// on Windows), so a viewer's (x,y) is the global point (x + origin.x, y + origin.y).

#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        struct MacDisplay
        {
            CGDirectDisplayID id = 0;
            CGRect bounds = {}; // global points, rounded to whole points
        };

        // The displays now showing a desktop, the main one first. A set of mirrored displays shows as one. Empty if
        // the system can't say (for example while the displays are being reconfigured).
        inline std::vector<MacDisplay> ListMacDisplays()
        {
            constexpr uint32_t kMaxDisplays = 16;
            CGDirectDisplayID ids[kMaxDisplays];
            uint32_t count = 0;
            if (CGGetActiveDisplayList(kMaxDisplays, ids, &count) != kCGErrorSuccess)
            {
                return {};
            }
            std::vector<MacDisplay> displays;
            const CGDirectDisplayID main = CGMainDisplayID();
            for (uint32_t i = 0; i < count; ++i)
            {
                MacDisplay display;
                display.id = ids[i];
                CGRect bounds = CGDisplayBounds(ids[i]);
                display.bounds = CGRectMake(std::round(bounds.origin.x), std::round(bounds.origin.y),
                                            std::round(bounds.size.width), std::round(bounds.size.height));
                if (display.bounds.size.width >= 1 && display.bounds.size.height >= 1)
                {
                    displays.push_back(display);
                }
            }
            std::stable_sort(displays.begin(), displays.end(), [main](const MacDisplay& a, const MacDisplay& b) {
                return (a.id == main) && (b.id != main);
            });
            return displays;
        }

        // The smallest rectangle holding every display, in global points.
        inline CGRect MacDesktopBounds(const std::vector<MacDisplay>& displays)
        {
            CGRect all = CGRectNull;
            for (const auto& display : displays)
            {
                all = CGRectUnion(all, display.bounds);
            }
            return all;
        }
    } // namespace Server
} // namespace Marconi
