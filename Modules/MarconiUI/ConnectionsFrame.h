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
class wxStaticText;
class wxToggleButton;

namespace MarconiUI
{
    class ConnectionsFrame : public wxFrame
    {
    public:
        ConnectionsFrame();

    private:
        void Reload(const wxString& selectId = wxEmptyString);
        void UpdateButtons();
        void ApplyAppearance();
        void ChooseAppearance(int selection);
        void ShowSettings();
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
        wxTextCtrl* m_search = nullptr;
        wxStaticText* m_count = nullptr;
        wxStaticText* m_empty = nullptr;
        wxStaticText* m_notice = nullptr;
        wxToggleButton* m_themeButtons[3] = {};
        std::vector<wxStaticText*> m_mutedLabels;
        std::vector<wxButton*> m_primaryButtons;
        wxListView* m_list = nullptr;
        wxButton* m_connect = nullptr;
        wxButton* m_edit = nullptr;
        wxButton* m_delete = nullptr;
    };
} // namespace MarconiUI
