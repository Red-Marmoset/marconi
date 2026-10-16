// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Prompt for a VNC password, or an account name and password for Apple Remote Desktop servers

#include "CredentialsDialog.h"
#include "Appearance.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace MarconiUI
{
    CredentialsDialog::CredentialsDialog(wxWindow* parent, const wxString& serverName,
                                         Marconi::Rfb::CredentialsKind kind, const wxString& username,
                                         bool offerRemember)
        : wxDialog(parent, wxID_ANY, "Authentication required")
    {
        using Marconi::Rfb::CredentialsKind;
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        wxString prompt;
        switch (kind)
        {
        case CredentialsKind::Account:
            prompt = wxString::Format("Log in to %s with an account on that computer:", serverName);
            break;
        case CredentialsKind::PasswordOrAccount:
            prompt = wxString::Format("Log in to %s with an account on that computer, or leave the user name empty\n"
                                      "to use the VNC password:",
                                      serverName);
            break;
        default:
            prompt = wxString::Format("Enter the VNC password for %s:", serverName);
            break;
        }
        const bool needUsername = kind != CredentialsKind::Password;
        outer->Add(new wxStaticText(this, wxID_ANY, prompt), 0, wxALL, FromDIP(20));

        wxFlexGridSizer* grid = new wxFlexGridSizer(2, FromDIP(wxSize(8, 8)));
        grid->AddGrowableCol(1);
        if (needUsername)
        {
            grid->Add(new wxStaticText(this, wxID_ANY, "User name:"), 0, wxALIGN_CENTER_VERTICAL);
            m_username = new wxTextCtrl(this, wxID_ANY, username, wxDefaultPosition, FromDIP(wxSize(240, -1)));
            if (kind == CredentialsKind::PasswordOrAccount)
            {
                m_username->SetHint("optional");
            }
            grid->Add(m_username, 1, wxEXPAND);
        }
        grid->Add(new wxStaticText(this, wxID_ANY, "Password:"), 0, wxALIGN_CENTER_VERTICAL);
        m_password = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(240, -1)),
                                    wxTE_PASSWORD);
        grid->Add(m_password, 1, wxEXPAND);
        if (offerRemember)
        {
            grid->AddSpacer(0);
            m_remember = new wxCheckBox(this, wxID_ANY, "Remember password in the system keychain");
            grid->Add(m_remember);
        }
        outer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

        outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(20));
        SetSizerAndFit(outer);
        StyleWindow(this);
        CentreOnParent();

        if (m_username != nullptr && username.empty())
        {
            m_username->SetFocus();
        }
        else
        {
            m_password->SetFocus();
        }
    }

    wxString CredentialsDialog::GetUsername() const
    {
        return m_username != nullptr ? m_username->GetValue() : wxString();
    }

    wxString CredentialsDialog::GetPassword() const
    {
        return m_password->GetValue();
    }

    bool CredentialsDialog::GetRemember() const
    {
        return m_remember != nullptr && m_remember->GetValue();
    }
} // namespace MarconiUI
