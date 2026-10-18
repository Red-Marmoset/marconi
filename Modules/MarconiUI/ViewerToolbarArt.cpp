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
        if (!disabled && (checked || hovered))
        {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(checked ? palette.accent : palette.selection));
            gc->DrawRoundedRectangle(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, window->FromDIP(8));
        }
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
        return wxSize(text.x + window->FromDIP(24), std::max(text.y + window->FromDIP(14), window->FromDIP(36)));
    }
} // namespace MarconiUI
