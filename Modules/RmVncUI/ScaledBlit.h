// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Smooth scaled copy between DCs (wx's MSW StretchBlit forces nearest-neighbour; Windows needs HALFTONE)

#pragma once

#include <wx/dc.h>

namespace RmVncUI
{
    void ScaledBlit(wxDC& destination, const wxRect& destinationRect, wxDC& source, const wxRect& sourceRect);
} // namespace RmVncUI
