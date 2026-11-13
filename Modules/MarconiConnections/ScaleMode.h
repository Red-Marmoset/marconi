// Copyright (c) 2026 Red Marmoset Studios Ltd.
// How a viewer window fits the remote desktop, as saved with each connection

#pragma once

namespace MarconiUI
{
    enum class ScaleMode
    {
        Fit,   // scale the whole desktop into the window, keeping its aspect ratio
        Fixed, // a zoom level from Marconi::Zoom::kLevels (100 = one desktop pixel per screen pixel), scroll if needed
    };
} // namespace MarconiUI
