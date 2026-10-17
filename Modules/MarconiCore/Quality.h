// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Picture quality choices: how hard the server compresses, and whether it may lose detail to save bandwidth

#pragma once

#include <string>

namespace Marconi
{
    enum class PictureQuality
    {
        Best,         // no loss of detail
        Balanced,     // light JPEG for photos and video, sharp text (the default)
        LowBandwidth, // stronger compression and JPEG, for slow links
    };

    struct QualitySettings
    {
        int compressLevel = 2; // zlib level hint 0-9
        int qualityLevel = 8;  // Tight JPEG quality 0-9, or -1 for none (lossless only)
    };

    QualitySettings SettingsFor(PictureQuality quality);

    // "best", "balanced" or "low", for saving; anything else reads as Balanced.
    const char* QualityName(PictureQuality quality);
    PictureQuality QualityFromName(const std::string& name);
} // namespace Marconi
