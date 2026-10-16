// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi window icon, drawn at runtime on every platform. Resources/Marconi.ico (Tools/make_app_icon.py) is
// the same picture for the Windows executable and installer.

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
        // Signal: a mirrored M with two circular connection terminals.
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
                    const wxColour crimson(178, 34, 52);
                    wxGraphicsPenInfo stroke(crimson, 4.0 * s);
                    stroke.Cap(wxCAP_ROUND).Join(wxJOIN_ROUND);
                    gc->SetPen(gc->CreatePen(stroke));
                    gc->SetBrush(*wxTRANSPARENT_BRUSH);

                    // Every point is mirrored around x = 16; peaks and terminals share their y coordinates.
                    wxGraphicsPath signal = gc->CreatePath();
                    signal.MoveToPoint(5 * s, 23 * s);
                    signal.AddLineToPoint(10.5 * s, 9 * s);
                    signal.AddLineToPoint(16 * s, 20 * s);
                    signal.AddLineToPoint(21.5 * s, 9 * s);
                    signal.AddLineToPoint(27 * s, 23 * s);
                    gc->StrokePath(signal);

                    gc->SetPen(*wxTRANSPARENT_PEN);
                    gc->SetBrush(wxBrush(crimson));
                    const double radius = 3 * s;
                    for (double x : {5.0, 27.0})
                    {
                        gc->DrawEllipse(x * s - radius, 23 * s - radius, 2 * radius, 2 * radius);
                    }
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
            for (int size : {16, 20, 24, 32, 48, 64, 96, 128, 256})
            {
                bundle.AddIcon(DrawIcon(size));
            }
            return bundle;
        }();
        return icons;
    }
} // namespace MarconiUI
