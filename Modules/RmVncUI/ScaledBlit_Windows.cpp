// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Smooth scaled copy between DCs: GDI StretchBlt in HALFTONE mode

#include "ScaledBlit.h"

#include <wx/msw/private.h>

namespace RmVncUI
{
    void ScaledBlit(wxDC& destination, const wxRect& destinationRect, wxDC& source, const wxRect& sourceRect)
    {
        HDC target = static_cast<HDC>(destination.GetHDC());
        HDC from = static_cast<HDC>(source.GetHDC());
        if (target == nullptr || from == nullptr)
        {
            destination.StretchBlit(destinationRect.GetPosition(), destinationRect.GetSize(), &source,
                                    sourceRect.GetPosition(), sourceRect.GetSize());
            return;
        }
        // Device coordinates: account for the destination DC's logical origin (scrolling).
        wxPoint origin = destination.LogicalToDevice(destinationRect.GetPosition());
        int oldMode = ::SetStretchBltMode(target, HALFTONE);
        ::SetBrushOrgEx(target, 0, 0, nullptr);
        ::StretchBlt(target, origin.x, origin.y, destinationRect.width, destinationRect.height, from, sourceRect.x,
                     sourceRect.y, sourceRect.width, sourceRect.height, SRCCOPY);
        ::SetStretchBltMode(target, oldMode);
    }
} // namespace RmVncUI
