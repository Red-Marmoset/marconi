// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Theme-aware painting for the viewer's dockable toolbar.

#pragma once

#include <wx/aui/auibar.h>

namespace MarconiUI
{
    class ViewerToolbarArt : public wxAuiGenericToolBarArt
    {
    public:
        wxAuiToolBarArt* Clone() override;
        void DrawBackground(wxDC& dc, wxWindow* window, const wxRect& rect) override;
        void DrawPlainBackground(wxDC& dc, wxWindow* window, const wxRect& rect) override;
        void DrawButton(wxDC& dc, wxWindow* window, const wxAuiToolBarItem& item, const wxRect& rect) override;
        void DrawSeparator(wxDC& dc, wxWindow* window, const wxRect& rect) override;
        void DrawGripper(wxDC& dc, wxWindow* window, const wxRect& rect) override;
        void DrawOverflowButton(wxDC& dc, wxWindow* window, const wxRect& rect, int state) override;
        wxSize GetToolSize(wxReadOnlyDC& dc, wxWindow* window, const wxAuiToolBarItem& item) override;
    };
} // namespace MarconiUI
