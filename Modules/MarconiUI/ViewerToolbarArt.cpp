// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Calm toolbar spacing and red checked states in either appearance.

#include "ViewerToolbarArt.h"
#include "Appearance.h"

#include <wx/graphics.h>
#include <wx/dc.h>
#include <wx/aui/framemanager.h>

#include <algorithm>
#include <memory>

namespace MarconiUI
{
    wxAuiToolBarArt* ViewerToolbarArt::Clone()
    {
        return new ViewerToolbarArt(*this);
    }

    void ViewerToolbarArt::DrawBackground(wxDC& dc, wxWindow*, const wxRect& rect)
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(GetThemePalette().background));
        dc.DrawRectangle(rect);
    }

    void ViewerToolbarArt::DrawPlainBackground(wxDC& dc, wxWindow* window, const wxRect& rect)
    {
        DrawBackground(dc, window, rect);
    }

    void ViewerToolbarArt::DrawButton(wxDC& dc, wxWindow* window,
                                      const wxAuiToolBarItem& item, const wxRect& rect)
    {
        const ThemePalette palette = GetThemePalette();
        const int state = item.GetState();
        const bool disabled = (state & wxAUI_BUTTON_STATE_DISABLED) != 0;
        const bool checked = (state & wxAUI_BUTTON_STATE_CHECKED) != 0;
        const bool hovered = (state & (wxAUI_BUTTON_STATE_HOVER | wxAUI_BUTTON_STATE_PRESSED)) != 0;
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (!gc)
        {
            wxAuiGenericToolBarArt::DrawButton(dc, window, item, rect);
            return;
        }
        // Every button gets a visible body, so the bar reads as a row of buttons rather than loose words.
        wxColour fill = checked ? palette.accent : hovered ? palette.selection : palette.surface;
        wxColour border = checked || hovered ? palette.accent : palette.border;
        if (disabled)
        {
            fill = palette.background;
            border = palette.border;
        }
        else if (checked && hovered)
        {
            fill = palette.accent.ChangeLightness(110);
        }
        const double inset = window->FromDIP(2);
        gc->SetPen(wxPen(border));
        gc->SetBrush(wxBrush(fill));
        gc->DrawRoundedRectangle(rect.x + inset, rect.y + inset, rect.width - 2 * inset, rect.height - 2 * inset,
                                 window->FromDIP(7));
        gc->SetFont(GetFont(), disabled ? palette.muted : checked ? *wxWHITE : palette.text);
        double width = 0, height = 0;
        gc->GetTextExtent(item.GetLabel(), &width, &height);
        gc->DrawText(item.GetLabel(), rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2);
    }

    void ViewerToolbarArt::DrawSeparator(wxDC& dc, wxWindow* window, const wxRect& rect)
    {
        dc.SetPen(wxPen(GetThemePalette().border));
        const int inset = window->FromDIP(8);
        if (GetFlags() & wxAUI_TB_VERTICAL)
        {
            dc.DrawLine(rect.x + inset, rect.y + rect.height / 2, rect.GetRight() - inset, rect.y + rect.height / 2);
        }
        else
        {
            dc.DrawLine(rect.x + rect.width / 2, rect.y + inset, rect.x + rect.width / 2, rect.GetBottom() - inset);
        }
    }

    void ViewerToolbarArt::DrawGripper(wxDC& dc, wxWindow* window, const wxRect& rect)
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(GetThemePalette().muted));
        for (int i = -1; i <= 1; ++i)
        {
            dc.DrawCircle(rect.x + rect.width / 2, rect.y + rect.height / 2 + window->FromDIP(i * 5), 1);
        }
    }

    void ViewerToolbarArt::DrawOverflowButton(wxDC& dc, wxWindow* window, const wxRect& rect, int state)
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush((state & wxAUI_BUTTON_STATE_HOVER) ? GetThemePalette().selection :
                                                               GetThemePalette().background));
        dc.DrawRectangle(rect);
        dc.SetBrush(wxBrush(GetThemePalette().text));
        for (int i = -1; i <= 1; ++i)
        {
            dc.DrawCircle(rect.x + rect.width / 2, rect.y + rect.height / 2 + window->FromDIP(i * 5), 1);
        }
    }

    wxSize ViewerToolbarArt::GetToolSize(wxReadOnlyDC& dc, wxWindow* window, const wxAuiToolBarItem& item)
    {
        dc.SetFont(GetFont());
        const wxSize text = dc.GetTextExtent(item.GetLabel());
        return wxSize(text.x + window->FromDIP(28), std::max(text.y + window->FromDIP(18), window->FromDIP(38)));
    }
} // namespace MarconiUI
