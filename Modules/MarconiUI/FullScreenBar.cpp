// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The small bar that slides in at the top of the screen in full screen mode, so the controls are a mouse move away

#include "FullScreenBar.h"

#include "Appearance.h"
#include "ViewerToolbarArt.h"

#include <wx/aui/auibar.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

namespace MarconiUI
{
    namespace
    {
        constexpr int kTickMs = 100;
        constexpr int kRevealTicks = 3;  // the pointer must rest at the top edge this long (300 ms)
        constexpr int kConcealTicks = 8; // and be away from the bar this long before it hides (800 ms)
    } // namespace

    FullScreenBar::FullScreenBar(wxWindow* parent, const wxString& title, const std::vector<Item>& items)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
    {
        Hide();
        int gap = FromDIP(6);
        wxFont font = GetFont();
        font.SetPointSize(10);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_title = new wxStaticText(this, wxID_ANY, title);
        m_title->SetFont(font);
        row->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, gap * 2);
        // Drawn like the window's toolbar
        m_buttons = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxAUI_TB_TEXT | wxAUI_TB_HORZ_LAYOUT);
        m_buttons->SetArtProvider(new ViewerToolbarArt());
        m_buttons->SetFont(font);
        m_buttons->SetMargins(0, FromDIP(4));
        m_buttons->SetToolPacking(FromDIP(6));
        m_buttons->SetToolBorderPadding(0);
        for (const Item& item : items)
        {
            m_buttons->AddTool(item.commandId, item.label, wxNullBitmap, item.tooltip);
        }
        m_buttons->AddSeparator();
        m_pinId = NewControlId();
        m_buttons->AddTool(m_pinId, "Pin", wxNullBitmap, "Keep this bar on screen", wxITEM_CHECK);
        m_buttons->Realize();
        m_buttons->Bind(wxEVT_TOOL, [this](wxCommandEvent& event) {
            if (event.GetId() == m_pinId)
            {
                return;
            }
            // Sent from this bar, so the viewer can open its menus here rather than under the hidden toolbar.
            wxCommandEvent command(wxEVT_MENU, event.GetId());
            command.SetEventObject(this);
            GetParent()->GetEventHandler()->ProcessEvent(command);
        });
        row->Add(m_buttons, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        SetSizerAndFit(row);
        StyleWindow(this);

        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnTick(); }, m_timer.GetId());
    }

    bool FullScreenBar::IsPinned() const
    {
        return m_buttons->GetToolToggled(m_pinId);
    }

    void FullScreenBar::Begin()
    {
        m_watching = true;
        m_ticksAtTop = 0;
        m_ticksAway = 0;
        if (IsPinned())
        {
            Reveal();
        }
        m_timer.Start(kTickMs);
    }

    void FullScreenBar::End()
    {
        m_watching = false;
        m_timer.Stop();
        Hide();
    }

    void FullScreenBar::PlaceAtTop()
    {
        wxSize size = GetBestSize();
        wxSize parent = GetParent()->GetClientSize();
        SetSize((parent.x - size.x) / 2, 0, size.x, size.y);
    }

    void FullScreenBar::Reveal()
    {
        PlaceAtTop();
        if (!IsShown())
        {
            Show();
        }
        Raise();
    }

    void FullScreenBar::Conceal()
    {
        if (IsShown() && !IsPinned())
        {
            Hide();
        }
    }

    void FullScreenBar::OnTick()
    {
        if (!m_watching)
        {
            return;
        }
        wxPoint mouse = GetParent()->ScreenToClient(wxGetMousePosition());
        wxSize parent = GetParent()->GetClientSize();
        bool inside = mouse.x >= 0 && mouse.x < parent.x && mouse.y >= 0 && mouse.y < parent.y;
        // Only the very top row of pixels reveals the bar, so a remote menu bar just below it isn't disturbed, and
        // not while a button is held (a drag that reaches the edge shouldn't pop it up).
        bool atTop = inside && mouse.y <= 1 && !wxGetMouseState().LeftIsDown();
        if (IsShown())
        {
            bool overBar = GetRect().Inflate(FromDIP(24)).Contains(mouse);
            m_ticksAway = overBar || atTop ? 0 : m_ticksAway + 1;
            if (m_ticksAway >= kConcealTicks)
            {
                Conceal();
            }
            return;
        }
        m_ticksAtTop = atTop ? m_ticksAtTop + 1 : 0;
        if (m_ticksAtTop >= kRevealTicks)
        {
            m_ticksAway = 0;
            Reveal();
        }
    }
} // namespace MarconiUI
