// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Nebula headings and rounded controls that preserve native button interaction and accessibility.

#include "Controls.h"
#include "Appearance.h"

#include <wx/fontenum.h>
#include <wx/brush.h>
#include <wx/pen.h>
#include <wx/dcclient.h>
#include <wx/graphics.h>

#include <algorithm>
#include <memory>

#ifdef __WXMSW__
#include <wx/msw/dc.h>
#include <wx/msw/wrapwin.h>
#include <wx/msw/wrapcctl.h>
#endif

namespace MarconiUI
{
    wxFont HeadingFont(const wxFont& base, float scale, bool heavy)
    {
        wxFont font = base.Scaled(scale);
        const wxString face = heavy ? "Nebula Sans Black" : "Nebula Sans Semibold";
        if (wxFontEnumerator::IsValidFacename(face))
        {
            font.SetFaceName(face);
            // Match the face's actual weight: GDI can otherwise choose an installed italic sibling.
            font.SetWeight(heavy ? wxFONTWEIGHT_HEAVY : wxFONTWEIGHT_SEMIBOLD);
        }
        else
        {
            if (wxFontEnumerator::IsValidFacename("Nebula Sans"))
            {
                font.SetFaceName("Nebula Sans");
            }
            font.SetWeight(heavy ? wxFONTWEIGHT_HEAVY : wxFONTWEIGHT_SEMIBOLD);
        }
        font.SetStyle(wxFONTSTYLE_NORMAL);
        return font;
    }

#ifdef __WXMSW__
    namespace
    {
        bool DrawNativeButton(wxWindow* window, WXDRAWITEMSTRUCT* nativeItem,
                              bool primary, bool selected, bool hovered)
        {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(nativeItem);
            const ThemePalette palette = GetThemePalette();
            const bool enabled = (item->itemState & ODS_DISABLED) == 0;
            const bool pressed = (item->itemState & ODS_SELECTED) != 0;
            const bool focused = (item->itemState & ODS_FOCUS) != 0;
            std::unique_ptr<wxGraphicsContext> gc(
                wxGraphicsContext::CreateFromNativeHDC(reinterpret_cast<WXHDC>(item->hDC)));
            if (!gc)
            {
                return false;
            }
            const wxRect bounds(item->rcItem.left, item->rcItem.top,
                                item->rcItem.right - item->rcItem.left, item->rcItem.bottom - item->rcItem.top);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(window->GetParent()->GetBackgroundColour()));
            gc->DrawRectangle(bounds.x, bounds.y, bounds.width, bounds.height);

            wxColour fill = primary ? palette.accent : selected ? palette.selection : palette.surface;
            wxColour text = primary ? *wxWHITE : palette.text;
            wxColour border = primary || selected ? palette.accent : palette.border;
            if (!enabled)
            {
                fill = palette.background;
                text = palette.muted;
                border = palette.border;
            }
            else if (pressed)
            {
                fill = primary ? palette.accent.ChangeLightness(88) : palette.selection;
                border = palette.accent;
            }
            else if (hovered)
            {
                fill = primary ? palette.accent.ChangeLightness(108) : palette.selection;
                border = palette.accent;
            }
            const double inset = window->FromDIP(3);
            const double radius = window->FromDIP(9);
            if (focused && enabled)
            {
                gc->SetPen(wxPen(palette.accent, window->FromDIP(2)));
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->DrawRoundedRectangle(bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2, radius + 2);
            }
            gc->SetPen(wxPen(border));
            gc->SetBrush(wxBrush(fill));
            gc->DrawRoundedRectangle(bounds.x + inset, bounds.y + inset,
                                     bounds.width - 2 * inset, bounds.height - 2 * inset, radius);
            gc->SetFont(window->GetFont(), text);
            const wxString label = wxControl::RemoveMnemonics(window->GetLabel());
            double width = 0, height = 0;
            gc->GetTextExtent(label, &width, &height);
            gc->DrawText(label, bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2);
            return true;
        }
    }
#endif

    ActionButton::ActionButton(wxWindow* parent, wxWindowID id, const wxString& label, bool primary,
                               const wxSize& size)
        : wxButton(parent, id, label, wxDefaultPosition,
                   parent->FromDIP(size == wxDefaultSize ? wxSize(-1, 40) : size)),
          m_primary(primary)
    {
        SetFont(HeadingFont(GetFont()));
        SetName(primary ? "MarconiPrimaryButton" : "MarconiActionButton");
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) { m_hovered = true; Refresh(); event.Skip(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) { m_hovered = false; Refresh(); event.Skip(); });
#ifdef __WXMSW__
        MakeOwnerDrawn();
#endif
    }

    AppearanceButton::AppearanceButton(wxWindow* parent, const wxString& label)
        : wxToggleButton(parent, wxID_ANY, label, wxDefaultPosition, parent->FromDIP(wxSize(72, 36)))
    {
        SetFont(HeadingFont(GetFont()));
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) { m_hovered = true; Refresh(); event.Skip(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) { m_hovered = false; Refresh(); event.Skip(); });
#ifdef __WXMSW__
        MakeOwnerDrawn();
#endif
    }

#ifdef __WXMSW__
    bool ThemedListView::MSWOnNotify(int id, WXLPARAM param, WXLPARAM* result)
    {
        const auto* header = reinterpret_cast<NMHDR*>(param);
        if (header->hwndFrom == ListView_GetHeader(GetHwnd()) && header->code == NM_CUSTOMDRAW)
        {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(param);
            if (draw->dwDrawStage == CDDS_PREPAINT)
            {
                *result = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                return true;
            }
            if (draw->dwDrawStage == CDDS_POSTPAINT)
            {
                RECT client{};
                ::GetClientRect(header->hwndFrom, &client);
                int right = 0;
                const int count = Header_GetItemCount(header->hwndFrom);
                for (int i = 0; i < count; ++i)
                {
                    RECT item{};
                    Header_GetItemRect(header->hwndFrom, i, &item);
                    right = std::max(right, static_cast<int>(item.right));
                }
                if (right < client.right)
                {
                    const int saved = SaveDC(draw->hdc);
                    wxDCTemp dc(reinterpret_cast<WXHDC>(draw->hdc));
                    const ThemePalette palette = GetThemePalette();
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(palette.surface));
                    dc.DrawRectangle(right, 0, client.right - right, client.bottom);
                    dc.SetPen(wxPen(palette.border));
                    dc.DrawLine(right, client.bottom - 1, client.right, client.bottom - 1);
                    RestoreDC(draw->hdc, saved);
                }
                *result = CDRF_DODEFAULT;
                return true;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                wxListItem column;
                column.SetMask(wxLIST_MASK_TEXT | wxLIST_MASK_FORMAT);
                if (GetColumn(static_cast<int>(draw->dwItemSpec), column))
                {
                    std::unique_ptr<wxGraphicsContext> gc(
                        wxGraphicsContext::CreateFromNativeHDC(reinterpret_cast<WXHDC>(draw->hdc)));
                    if (gc)
                    {
                        const ThemePalette palette = GetThemePalette();
                        const wxRect rect(draw->rc.left, draw->rc.top,
                                          draw->rc.right - draw->rc.left, draw->rc.bottom - draw->rc.top);
                        gc->SetPen(*wxTRANSPARENT_PEN);
                        gc->SetBrush(wxBrush(palette.surface));
                        gc->DrawRectangle(rect.x, rect.y, rect.width, rect.height);
                        gc->Clip(rect.x, rect.y, rect.width, rect.height);
                        gc->SetFont(HeadingFont(GetFont()), palette.muted);
                        double width = 0, height = 0;
                        gc->GetTextExtent(column.GetText(), &width, &height);
                        const double x = column.GetAlign() == wxLIST_FORMAT_RIGHT ?
                            rect.GetRight() - width - FromDIP(8) : rect.x + FromDIP(8);
                        gc->DrawText(column.GetText(), x, rect.y + (rect.height - height) / 2);
                        gc->SetPen(wxPen(palette.border));
                        gc->StrokeLine(rect.x, rect.GetBottom(), rect.GetRight(), rect.GetBottom());
                        *result = CDRF_SKIPDEFAULT;
                        return true;
                    }
                }
            }
        }
        return wxListView::MSWOnNotify(id, param, result);
    }

    WXLRESULT ThemedStatusBar::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            const HDC hdc = BeginPaint(GetHwnd(), &paint);
            {
                const int saved = SaveDC(hdc);
                wxDCTemp dc(reinterpret_cast<WXHDC>(hdc));
                const ThemePalette palette = GetThemePalette();
                dc.SetBackground(wxBrush(palette.background));
                dc.Clear();
                dc.SetFont(GetFont());
                dc.SetTextForeground(palette.muted);
                dc.SetPen(wxPen(palette.border));
                dc.DrawLine(0, 0, GetClientSize().x, 0);
                for (int i = 0; i < GetFieldsCount(); ++i)
                {
                    wxRect rect;
                    if (GetFieldRect(i, rect))
                    {
                        rect.Deflate(FromDIP(8), 0);
                        if (rect.width > 0)
                        {
                            wxDCClipper clip(dc, rect);
                            const wxString text = wxControl::Ellipsize(GetStatusText(i), dc,
                                wxELLIPSIZE_END, rect.width, wxELLIPSIZE_FLAGS_EXPAND_TABS);
                            dc.DrawLabel(text, rect, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
                        }
                    }
                }
                RestoreDC(hdc, saved);
            }
            EndPaint(GetHwnd(), &paint);
            return 0;
        }
        return wxStatusBar::MSWWindowProc(message, wParam, lParam);
    }

    bool ActionButton::MSWOnDraw(WXDRAWITEMSTRUCT* item)
    {
        return DrawNativeButton(this, item, m_primary, false, m_hovered) || wxButton::MSWOnDraw(item);
    }

    bool AppearanceButton::MSWOnDraw(WXDRAWITEMSTRUCT* item)
    {
        return DrawNativeButton(this, item, false, GetValue(), m_hovered) || wxToggleButton::MSWOnDraw(item);
    }
#endif
} // namespace MarconiUI
