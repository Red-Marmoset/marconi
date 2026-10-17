// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The bar shown across the top of the viewer while a lost connection is being retried

#include "ConnectionLostBar.h"

#include "Appearance.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace MarconiUI
{
    ConnectionLostBar::ConnectionLostBar(wxWindow* parent, std::function<void()> onRetry,
                                         std::function<void()> onDisconnect)
        : wxPanel(parent, wxID_ANY)
    {
        // A warning colour that reads in both appearances.
        SetBackgroundColour(wxColour(179, 98, 0));
        SetForegroundColour(*wxWHITE);
        m_message = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_message->SetForegroundColour(*wxWHITE);
        m_retry = new wxButton(this, wxID_ANY, "Retry Now");
        m_disconnect = new wxButton(this, wxID_ANY, "Disconnect");
        m_retry->Bind(wxEVT_BUTTON, [onRetry](wxCommandEvent&) { onRetry(); });
        m_disconnect->Bind(wxEVT_BUTTON, [onDisconnect](wxCommandEvent&) { onDisconnect(); });

        int gap = FromDIP(8);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(m_message, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
        row->Add(m_retry, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
        row->Add(m_disconnect, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, gap);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, gap / 2);
        SetSizer(outer);
    }

    void ConnectionLostBar::SetMessage(const wxString& message)
    {
        m_message->SetLabel(message);
        Layout();
    }

    void ConnectionLostBar::EnableRetry(bool enable)
    {
        m_retry->Enable(enable);
    }
} // namespace MarconiUI
