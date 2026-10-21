// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The performance readout

#include "PerformanceOverlay.h"

#include <wx/font.h>

#include <algorithm>

namespace MarconiUI
{
    namespace
    {
        constexpr int kMargin = 8;  // from the window's edge
        constexpr int kPadding = 6; // inside the box

        // A time in milliseconds, or a dash when not known.
        wxString Ms(double ms)
        {
            return ms < 0 ? wxString("-") : wxString::Format("%.1f ms", ms);
        }

        // Room for the clock line, which isn't in `lines` when the size is worked out.
        const wxString kClockSample = "Other computer's clock: 00000000";
    } // namespace

    wxArrayString FormatPerformance(bool detailed, const Marconi::PerformanceSnapshot& s,
                                    const Marconi::ConnectionHealth& health)
    {
        wxArrayString lines;
        lines.Add(health.roundTripMs >= 0 ? wxString::Format("Round trip: %d ms", health.roundTripMs)
                                          : wxString("Round trip: measuring"));
        if (!detailed)
        {
            lines.Add("Detailed timing needs a Marconi server.");
            return lines;
        }
        lines.Add(wxString::Format("Pictures: %.1f a second received, %.1f shown", s.framesPerSecond, s.shownPerSecond));
        lines.Add(wxString::Format("Data: %.0f kilobits a second", s.kilobitsPerSecond));
        lines.Add("Other computer: " + Ms(s.serverMs) + " (compressing " + Ms(s.compressMs) + ")");
        lines.Add("Network: " + Ms(s.networkMs) + ", decoding: " + Ms(s.decodeMs) + ", to screen: " + Ms(s.showMs));
        lines.Add("Age when shown: " + Ms(s.pipelineMs) + " (slowest 1%: " + Ms(s.pipelineP99Ms) + ")");
        return lines;
    }

    wxFont PerformanceFont()
    {
        return wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    }

    wxRect PerformanceOverlayRect(wxDC& dc, const wxSize& clientSize, const wxArrayString& lines)
    {
        dc.SetFont(PerformanceFont());
        int width = 0;
        int height = 0;
        int lineHeight = dc.GetCharHeight();
        for (const wxString& line : lines)
        {
            width = std::max(width, dc.GetTextExtent(line).x);
            height += lineHeight;
        }
        width = std::max(width, dc.GetTextExtent(kClockSample).x);
        height += lineHeight;
        width += 2 * kPadding;
        height += 2 * kPadding;
        return wxRect(clientSize.x - width - kMargin, kMargin, width, height);
    }

    void DrawPerformanceOverlay(wxDC& dc, const wxRect& rect, const wxArrayString& lines, const wxString& clockLine)
    {
        dc.SetFont(PerformanceFont());
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(16, 16, 16)));
        dc.DrawRectangle(rect);
        dc.SetTextForeground(wxColour(235, 235, 235));
        int y = rect.y + kPadding;
        const int lineHeight = dc.GetCharHeight();
        for (const wxString& line : lines)
        {
            dc.DrawText(line, rect.x + kPadding, y);
            y += lineHeight;
        }
        if (!clockLine.empty())
        {
            dc.SetTextForeground(wxColour(255, 230, 0)); // yellow, like the test pattern's clock
            dc.DrawText(clockLine, rect.x + kPadding, y);
        }
    }
} // namespace MarconiUI
