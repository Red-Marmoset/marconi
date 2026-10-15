// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Add / edit a saved connection

#include "ConnectionDialog.h"

#include <MarconiCore/Address.h>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace MarconiUI
{
    ConnectionDialog::ConnectionDialog(wxWindow* parent, const SavedConnection& connection, const wxString& password)
        : wxDialog(parent, wxID_ANY, connection.id.empty() ? "New Connection" : "Edit Connection"),
          m_connection(connection)
    {
        const int gap = FromDIP(8);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

        wxFlexGridSizer* grid = new wxFlexGridSizer(2, wxSize(gap, gap));
        grid->AddGrowableCol(1);
        auto addRow = [&](const wxString& label, wxWindow* control) {
            grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(control, 1, wxEXPAND);
        };
        wxSize fieldSize = FromDIP(wxSize(260, -1));
        m_name = new wxTextCtrl(this, wxID_ANY, connection.name, wxDefaultPosition, fieldSize);
        m_name->SetHint("optional, e.g. Studio Mac");
        addRow("Name:", m_name);
        m_address = new wxTextCtrl(this, wxID_ANY, connection.address, wxDefaultPosition, fieldSize);
        m_address->SetHint("host, host:display or host::port");
        addRow("Address:", m_address);
        m_username = new wxTextCtrl(this, wxID_ANY, connection.username, wxDefaultPosition, fieldSize);
        m_username->SetHint("only for Macs (account login)");
        addRow("User name:", m_username);
        m_password = new wxTextCtrl(this, wxID_ANY, password, wxDefaultPosition, fieldSize, wxTE_PASSWORD);
        addRow("Password:", m_password);
        grid->AddSpacer(0);
        m_rememberPassword = new wxCheckBox(this, wxID_ANY, "Remember password in the system keychain");
        m_rememberPassword->SetValue(connection.rememberPassword);
        grid->Add(m_rememberPassword);
        outer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(12));

        wxStaticBoxSizer* view = new wxStaticBoxSizer(wxVERTICAL, this, "Viewer");
        wxBoxSizer* scaleRow = new wxBoxSizer(wxHORIZONTAL);
        scaleRow->Add(new wxStaticText(view->GetStaticBox(), wxID_ANY, "Size:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        m_scale = new wxChoice(view->GetStaticBox(), wxID_ANY);
        m_scale->Append("Scale to fit the window");
        m_scale->Append("Actual size (100%)");
        m_scale->SetSelection(connection.scaleMode == ScaleMode::Actual ? 1 : 0);
        scaleRow->Add(m_scale, 1);
        view->Add(scaleRow, 0, wxEXPAND | wxALL, gap);
        m_viewOnly = new wxCheckBox(view->GetStaticBox(), wxID_ANY, "View only (don't send keyboard or mouse)");
        m_viewOnly->SetValue(connection.viewOnly);
        view->Add(m_viewOnly, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        m_clipboard = new wxCheckBox(view->GetStaticBox(), wxID_ANY, "Share the clipboard");
        m_clipboard->SetValue(connection.clipboard);
        view->Add(m_clipboard, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        outer->Add(view, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

        outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
        SetSizerAndFit(outer);
        CentreOnParent();

        if (!ConnectionStore::IsKeychainAvailable())
        {
            m_rememberPassword->SetValue(false);
            m_rememberPassword->Disable();
            m_rememberPassword->SetToolTip("No system keychain is available on this computer");
        }
        m_rememberPassword->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdatePasswordState(); });
        UpdatePasswordState();
        (connection.address.empty() ? m_address : m_name)->SetFocus();
    }

    void ConnectionDialog::UpdatePasswordState()
    {
        // Without "remember" there is nowhere to keep the password: Marconi asks when connecting instead.
        m_password->Enable(m_rememberPassword->GetValue());
    }

    bool ConnectionDialog::TransferDataFromWindow()
    {
        Marconi::Address address;
        std::string error;
        if (!Marconi::ParseAddress(m_address->GetValue().utf8_string(), address, error))
        {
            wxMessageBox(wxString::FromUTF8(error), GetTitle(), wxOK | wxICON_WARNING, this);
            m_address->SetFocus();
            return false;
        }
        return true;
    }

    SavedConnection ConnectionDialog::GetConnection() const
    {
        SavedConnection connection = m_connection;
        connection.name = m_name->GetValue().Strip(wxString::both);
        connection.address = m_address->GetValue().Strip(wxString::both);
        connection.username = m_username->GetValue().Strip(wxString::both);
        connection.rememberPassword = m_rememberPassword->GetValue();
        connection.scaleMode = m_scale->GetSelection() == 1 ? ScaleMode::Actual : ScaleMode::Fit;
        connection.viewOnly = m_viewOnly->GetValue();
        connection.clipboard = m_clipboard->GetValue();
        return connection;
    }

    wxString ConnectionDialog::GetPassword() const
    {
        return m_rememberPassword->GetValue() ? m_password->GetValue() : wxString();
    }
} // namespace MarconiUI
