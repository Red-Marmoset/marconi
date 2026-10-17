// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The zoom levels the viewer offers besides fitting the window, and stepping between them

#include <MarconiCore/Zoom.h>

namespace Marconi
{
    namespace Zoom
    {
        int Next(int currentPercent, bool in)
        {
            if (in)
            {
                for (int level : kLevels)
                {
                    if (level > currentPercent)
                    {
                        return level;
                    }
                }
                return kLevels[kLevelCount - 1];
            }
            for (size_t i = kLevelCount; i > 0; --i)
            {
                if (kLevels[i - 1] < currentPercent)
                {
                    return kLevels[i - 1];
                }
            }
            return kLevels[0];
        }

        bool IsLevel(int percent)
        {
            for (int level : kLevels)
            {
                if (level == percent)
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace Zoom
} // namespace Marconi
