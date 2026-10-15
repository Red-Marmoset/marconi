// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Smooth scaled copy between DCs: wx's StretchBlit already interpolates on macOS (CoreGraphics) and GTK (cairo)

#include "ScaledBlit.h"

namespace MarconiUI
{
    void ScaledBlit(wxDC& destination, const wxRect& destinationRect, wxDC& source, const wxRect& sourceRect)
    {
        destination.StretchBlit(destinationRect.GetPosition(), destinationRect.GetSize(), &source,
                                sourceRect.GetPosition(), sourceRect.GetSize());
    }
} // namespace MarconiUI
