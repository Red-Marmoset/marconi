// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The small bar that slides in at the top of the screen in full screen mode, so the controls are a mouse move away

#include "FullScreenBar.h"

#include "Appearance.h"

#include <wx/button.h>
#include <wx/display.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
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
        StyleWindow(this);
        int gap = FromDIP(6);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_title = new wxStaticText(this, wxID_ANY, title);
        wxFont bold = m_title->GetFont();
        bold.MakeBold();
        m_title->SetFont(bold);
        row->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, gap * 2);
        for (const Item& item : items)
        {
            wxButton* button = new wxButton(this, wxID_ANY, item.label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            button->SetToolTip(item.tooltip);
            int commandId = item.commandId;
            button->Bind(wxEVT_BUTTON, [this, commandId](wxCommandEvent&) {
                wxCommandEvent command(wxEVT_MENU, commandId);
                command.SetEventObject(this);
                GetParent()->GetEventHandler()->ProcessEvent(command);
            });
            row->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        }
        m_pin = new wxToggleButton(this, wxID_ANY, "Pin", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        m_pin->SetToolTip("Keep this bar on screen");
        row->Add(m_pin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap * 2);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, gap / 2);
        SetSizerAndFit(outer);

        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnTick(); }, m_timer.GetId());
    }

    bool FullScreenBar::IsPinned() const
    {
        return m_pin->GetValue();
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
