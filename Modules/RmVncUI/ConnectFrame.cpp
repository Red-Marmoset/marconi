// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: type an address and connect

#include "ConnectFrame.h"

#include "ViewerFrame.h"

#include <RmVncCore/Address.h>

#include <wx/button.h>
#include <wx/config.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace RmVncUI
{
    namespace
    {
        const char* kLastAddressKey = "/Connect/LastAddress";
    }

    ConnectFrame::ConnectFrame()
        : wxFrame(nullptr, wxID_ANY, "rmvnc", wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX))
    {
        wxPanel* panel = new wxPanel(this);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

        outer->Add(new wxStaticText(panel, wxID_ANY, "VNC server (host, host:display or host::port):"), 0,
                   wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        m_address = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(280, -1)),
                                   wxTE_PROCESS_ENTER);
        m_address->SetValue(wxConfigBase::Get()->Read(kLastAddressKey, wxEmptyString));
        m_address->SelectAll();
        wxButton* connect = new wxButton(panel, wxID_OK, "Connect");
        connect->SetDefault();
        row->Add(m_address, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        row->Add(connect, 0, wxALIGN_CENTER_VERTICAL);
        outer->Add(row, 0, wxEXPAND | wxALL, FromDIP(12));

        panel->SetSizer(outer);
        wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(panel, 1, wxEXPAND);
        SetSizerAndFit(frameSizer);
        Centre();

        connect->Bind(wxEVT_BUTTON, &ConnectFrame::OnConnect, this);
        m_address->Bind(wxEVT_TEXT_ENTER, &ConnectFrame::OnConnect, this);
        m_address->SetFocus();
    }

    void ConnectFrame::OnConnect(wxCommandEvent&)
    {
        RmVnc::Address address;
        std::string error;
        if (!RmVnc::ParseAddress(m_address->GetValue().utf8_string(), address, error))
        {
            wxMessageBox(wxString::FromUTF8(error), "rmvnc", wxOK | wxICON_WARNING, this);
            return;
        }
        wxConfigBase::Get()->Write(kLastAddressKey, m_address->GetValue());

        ViewerOptions options;
        options.address = address;
        ViewerFrame* viewer = new ViewerFrame(options);
        viewer->Show();
    }
} // namespace RmVncUI
