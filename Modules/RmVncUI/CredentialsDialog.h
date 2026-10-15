// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Prompt for a VNC password, or an account name and password for Apple Remote Desktop servers

#pragma once

#include <wx/dialog.h>

class wxTextCtrl;

namespace RmVncUI
{
    class CredentialsDialog : public wxDialog
    {
    public:
        CredentialsDialog(wxWindow* parent, const wxString& serverName, bool needUsername, const wxString& username);

        wxString GetUsername() const;
        wxString GetPassword() const;

    private:
        wxTextCtrl* m_username = nullptr;
        wxTextCtrl* m_password = nullptr;
    };
} // namespace RmVncUI
