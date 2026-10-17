// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Picture quality choices: how hard the server compresses, and whether it may lose detail to save bandwidth

#include <MarconiCore/Quality.h>

namespace Marconi
{
    QualitySettings SettingsFor(PictureQuality quality)
    {
        switch (quality)
        {
            case PictureQuality::Best: return {2, -1};
            case PictureQuality::LowBandwidth: return {6, 4};
            default: return {2, 8};
        }
    }

    const char* QualityName(PictureQuality quality)
    {
        switch (quality)
        {
            case PictureQuality::Best: return "best";
            case PictureQuality::LowBandwidth: return "low";
            default: return "balanced";
        }
    }

    PictureQuality QualityFromName(const std::string& name)
    {
        if (name == "best")
        {
            return PictureQuality::Best;
        }
        if (name == "low")
        {
            return PictureQuality::LowBandwidth;
        }
        return PictureQuality::Balanced;
    }
} // namespace Marconi
