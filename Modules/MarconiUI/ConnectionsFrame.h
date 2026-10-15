// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: quick connect box plus the address book of saved connections

#pragma once

#include "ConnectionStore.h"

#include <wx/frame.h>

#include <vector>

class wxButton;
class wxListEvent;
class wxListView;
class wxTextCtrl;

namespace MarconiUI
{
    class ConnectionsFrame : public wxFrame
    {
    public:
        ConnectionsFrame();

    private:
        void Reload(const wxString& selectId = wxEmptyString);
        void UpdateButtons();
        int GetSelectedIndex() const;

        void OnQuickConnect(wxCommandEvent& event);
        void OnConnectSelected(wxCommandEvent& event);
        void OnNew(wxCommandEvent& event);
        void OnEdit(wxCommandEvent& event);
        void OnDelete(wxCommandEvent& event);
        void OnItemActivated(wxListEvent& event);
        void OnListKey(wxListEvent& event);

        void Connect(const SavedConnection& connection);
        bool EditConnection(SavedConnection& connection);

        ConnectionStore m_store;
        std::vector<SavedConnection> m_connections;
        wxTextCtrl* m_address = nullptr;
        wxListView* m_list = nullptr;
        wxButton* m_connect = nullptr;
        wxButton* m_edit = nullptr;
        wxButton* m_delete = nullptr;
    };
} // namespace MarconiUI
