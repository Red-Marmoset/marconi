// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi window icon, drawn at runtime (BuildTool has no resource compiler step)

#include "AppIcon.h"

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/icon.h>
#include <wx/image.h>

#include <algorithm>
#include <memory>

namespace MarconiUI
{
    namespace
    {
        // A monitor showing a smaller window with a pointer: "a screen inside a screen".
        wxIcon DrawIcon(int size)
        {
            wxImage image(size, size);
            image.InitAlpha();
            std::fill_n(image.GetAlpha(), size * size, 0);
            wxBitmap bitmap(image);
            {
                wxMemoryDC dc(bitmap);
                std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
                if (gc)
                {
                    const double s = size / 32.0;
                    // Bezel.
                    gc->SetPen(*wxTRANSPARENT_PEN);
                    gc->SetBrush(wxBrush(wxColour(178, 34, 52)));
                    gc->DrawRoundedRectangle(1 * s, 3 * s, 30 * s, 21 * s, 3 * s);
                    // Stand.
                    gc->DrawRectangle(13 * s, 24 * s, 6 * s, 3 * s);
                    gc->DrawRoundedRectangle(8 * s, 27 * s, 16 * s, 3 * s, 1.5 * s);
                    // Screen.
                    gc->SetBrush(wxBrush(wxColour(32, 36, 44)));
                    gc->DrawRectangle(3 * s, 5 * s, 26 * s, 17 * s);
                    // Remote window.
                    gc->SetBrush(wxBrush(wxColour(236, 240, 245)));
                    gc->DrawRoundedRectangle(7 * s, 8 * s, 18 * s, 11 * s, 1.5 * s);
                    gc->SetBrush(wxBrush(wxColour(178, 34, 52)));
                    gc->DrawRectangle(7 * s, 8 * s, 18 * s, 2.5 * s);
                    // Pointer.
                    wxGraphicsPath pointer = gc->CreatePath();
                    pointer.MoveToPoint(17 * s, 12 * s);
                    pointer.AddLineToPoint(17 * s, 21 * s);
                    pointer.AddLineToPoint(19.5 * s, 18.5 * s);
                    pointer.AddLineToPoint(22.5 * s, 20.5 * s);
                    pointer.AddLineToPoint(23.5 * s, 19 * s);
                    pointer.AddLineToPoint(20.5 * s, 17 * s);
                    pointer.AddLineToPoint(23.5 * s, 15.5 * s);
                    pointer.CloseSubpath();
                    gc->SetPen(wxPen(wxColour(32, 36, 44), std::max(1.0, s)));
                    gc->SetBrush(*wxWHITE_BRUSH);
                    gc->DrawPath(pointer);
                }
            }
            wxIcon icon;
            icon.CopyFromBitmap(bitmap);
            return icon;
        }
    } // namespace

    const wxIconBundle& GetAppIcons()
    {
        static wxIconBundle icons = []() {
            wxIconBundle bundle;
            for (int size : {16, 20, 24, 32, 48, 64, 128, 256})
            {
                bundle.AddIcon(DrawIcon(size));
            }
            return bundle;
        }();
        return icons;
    }
} // namespace MarconiUI
