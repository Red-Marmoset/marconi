// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Add / edit a saved connection

#pragma once

#include "ConnectionStore.h"

#include <wx/dialog.h>

class wxCheckBox;
class wxChoice;
class wxComboBox;
class wxTextCtrl;

namespace MarconiUI
{
    class ConnectionDialog : public wxDialog
    {
    public:
        // `password` is the stored password (if any), shown masked so it can be changed or cleared.
        ConnectionDialog(wxWindow* parent, const SavedConnection& connection, const wxString& password);

        SavedConnection GetConnection() const;
        wxString GetPassword() const;

    private:
        bool TransferDataFromWindow() override;
        void UpdatePasswordState();

        SavedConnection m_connection;
        wxTextCtrl* m_name = nullptr;
        wxTextCtrl* m_address = nullptr;
        wxTextCtrl* m_username = nullptr;
        wxTextCtrl* m_password = nullptr;
        wxCheckBox* m_rememberPassword = nullptr;
        wxChoice* m_scale = nullptr;
        wxCheckBox* m_viewOnly = nullptr;
        wxCheckBox* m_clipboard = nullptr;
        wxCheckBox* m_systemKeys = nullptr;
        wxChoice* m_quality = nullptr;
        wxCheckBox* m_lock = nullptr;
        wxCheckBox* m_sshTunnel = nullptr;
        wxCheckBox* m_favourite = nullptr;
        wxTextCtrl* m_mac = nullptr;
        wxTextCtrl* m_notes = nullptr;
        wxComboBox* m_group = nullptr;
    };
} // namespace MarconiUI
