// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: quick connect box, the address book of saved connections, and machines found on the network

#pragma once

#include "ConnectionStore.h"

#include <MarconiCore/Net/Discovery.h>

#include <wx/frame.h>
#include <wx/timer.h>

#include <atomic>
#include <thread>
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
        ~ConnectionsFrame() override; // joins the discovery thread

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

        // "On this network": machines advertising Screen Sharing, RDP or SSH, refreshed every 10 seconds.
        struct NetworkEntry
        {
            Marconi::Net::DiscoveredService service;
            int missedRounds = 0; // dropped after missing 3 rounds in a row
        };
        void StartDiscovery();
        void HandleDiscovery(const std::vector<Marconi::Net::DiscoveredService>& services);
        void ShowNetwork();
        void OnNetworkActivated(wxListEvent& event);

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
        wxListView* m_network = nullptr;
        wxStaticText* m_networkEmpty = nullptr;
        std::vector<NetworkEntry> m_networkEntries;
        wxTimer m_discoveryTimer;
        std::thread m_discoveryThread;
        std::atomic<bool> m_discoveryCancel{false};
        std::atomic<bool> m_discovering{false};
    };
} // namespace MarconiUI
