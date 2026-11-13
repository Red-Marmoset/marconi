// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The bar that offers to update an older Marconi Server

#include "ServerUpdateBar.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace MarconiUI
{
    ServerUpdateBar::ServerUpdateBar(wxWindow* parent) : wxPanel(parent, wxID_ANY)
    {
        // A calm blue (not the lost connection's warning colour) that reads in both appearances.
        SetBackgroundColour(wxColour(0x1E, 0x66, 0xD0));
        SetForegroundColour(*wxWHITE);
        m_message = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_message->SetForegroundColour(*wxWHITE);
        const int gap = FromDIP(8);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(m_message, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
        for (int i = 0; i < 3; ++i)
        {
            m_buttons[i] = new wxButton(this, wxID_ANY, wxEmptyString);
            m_buttons[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
                std::function<void()> action = m_actions[i]; // the action may change the bar's buttons
                if (action)
                {
                    action();
                }
            });
            row->Add(m_buttons[i], 0, wxALIGN_CENTER_VERTICAL | wxLEFT | (i == 2 ? wxRIGHT : 0), gap);
            m_buttons[i]->Hide();
        }
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, gap / 2);
        SetSizer(outer);
    }

    void ServerUpdateBar::SetContent(const wxString& message, const std::vector<Button>& buttons)
    {
        m_message->SetLabel(message);
        m_message->Wrap(-1);
        for (size_t i = 0; i < 3; ++i)
        {
            const bool used = i < buttons.size();
            m_actions[i] = used ? buttons[i].second : nullptr;
            m_buttons[i]->SetLabel(used ? buttons[i].first : wxString());
            m_buttons[i]->Show(used);
        }
        Layout();
        if (GetParent() != nullptr)
        {
            GetParent()->Layout();
        }
    }
} // namespace MarconiUI
