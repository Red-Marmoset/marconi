// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shared typography and native buttons with smooth, theme-aware Windows rendering.

#pragma once

#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/listctrl.h>
#include <wx/statusbr.h>

namespace MarconiUI
{
    wxFont HeadingFont(const wxFont& base, float scale = 1.0f, bool heavy = false);
    // base at a size given in Windows points, so text is the same size on screen everywhere: a Mac point is one
    // screen unit (72 to the inch), a Windows or Linux point is 4/3 of one (96 to the inch).
    wxFont SizedFont(const wxFont& base, double windowsPoints);

    class ThemedListView : public wxListView
    {
    public:
        using wxListView::wxListView;
#ifdef __WXMSW__
    protected:
        bool MSWOnNotify(int id, WXLPARAM param, WXLPARAM* result) override;
#endif
    };

    class ThemedStatusBar : public wxStatusBar
    {
    public:
        explicit ThemedStatusBar(wxWindow* parent) : wxStatusBar(parent) {}
#ifdef __WXMSW__
        WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
    };

    class ActionButton : public wxButton
    {
    public:
        ActionButton(wxWindow* parent, wxWindowID id, const wxString& label, bool primary = false,
                     const wxSize& size = wxDefaultSize);
#ifdef __WXMSW__
        bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override;
#endif

    private:
        bool m_primary = false;
        bool m_hovered = false;
    };

    class AppearanceButton : public wxToggleButton
    {
    public:
        AppearanceButton(wxWindow* parent, const wxString& label);
#ifdef __WXMSW__
        bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override;
#endif

    private:
        bool m_hovered = false;
    };
} // namespace MarconiUI
