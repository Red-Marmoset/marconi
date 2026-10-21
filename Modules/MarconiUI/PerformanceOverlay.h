// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The performance readout drawn over the picture (F8 > Show Performance): its wording and its drawing

#pragma once

#include <MarconiCore/RemoteSession.h>

#include <wx/arrstr.h>
#include <wx/dc.h>
#include <wx/gdicmn.h>

namespace MarconiUI
{
    // The readout's lines. `detailed` is false for connections that can't time pictures (VNC), which show the round
    // trip only. The other computer's clock is added at paint time (see DrawPerformanceOverlay).
    wxArrayString FormatPerformance(bool detailed, const Marconi::PerformanceSnapshot& snapshot,
                                    const Marconi::ConnectionHealth& health);

    // Where the readout goes in a window `clientSize` big, in window coordinates: the top-right corner.
    wxRect PerformanceOverlayRect(wxDC& dc, const wxSize& clientSize, const wxArrayString& lines);

    // Draws the readout in that rectangle. `clockLine`, if not empty, is drawn last (the other computer's clock,
    // which changes with every paint).
    void DrawPerformanceOverlay(wxDC& dc, const wxRect& rect, const wxArrayString& lines, const wxString& clockLine);

    // The font the readout uses (fixed width so the numbers don't jump about).
    wxFont PerformanceFont();
} // namespace MarconiUI
