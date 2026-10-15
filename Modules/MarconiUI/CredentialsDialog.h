// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Prompt for a VNC password, or an account name and password for Apple Remote Desktop servers

#pragma once

#include <MarconiCore/Rfb/Handshake.h>

#include <wx/dialog.h>

class wxCheckBox;
class wxTextCtrl;

namespace MarconiUI
{
    class CredentialsDialog : public wxDialog
    {
    public:
        // `offerRemember` adds a "Remember password" check box (for saved connections, when a keychain exists).
        CredentialsDialog(wxWindow* parent, const wxString& serverName, Marconi::Rfb::CredentialsKind kind,
                          const wxString& username, bool offerRemember);

        wxString GetUsername() const;
        wxString GetPassword() const;
        bool GetRemember() const;

    private:
        wxTextCtrl* m_username = nullptr;
        wxTextCtrl* m_password = nullptr;
        wxCheckBox* m_remember = nullptr;
    };
} // namespace MarconiUI
