// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: quick connect box plus the address book of saved connections

#include "ConnectionsFrame.h"

#include "AppIcon.h"
#include "ConnectionDialog.h"
#include "ViewerFrame.h"

#include <MarconiCore/Address.h>

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace MarconiUI
{
    namespace
    {
        wxString FormatLastUsed(const wxDateTime& when)
        {
            if (!when.IsValid())
            {
                return "Never";
            }
            wxDateTime today = wxDateTime::Today();
            if (when >= today)
            {
                return "Today " + when.Format("%H:%M");
            }
            if (when >= today - wxDateSpan::Day())
            {
                return "Yesterday " + when.Format("%H:%M");
            }
            return when.FormatDate();
        }
    } // namespace

    ConnectionsFrame::ConnectionsFrame() : wxFrame(nullptr, wxID_ANY, "Marconi")
    {
        SetIcons(GetAppIcons());
        wxPanel* panel = new wxPanel(this);
        const int border = FromDIP(12);
        const int gap = FromDIP(8);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

        // Quick connect.
        outer->Add(new wxStaticText(panel, wxID_ANY, "Connect to (host, host:display or host::port):"), 0,
                   wxLEFT | wxRIGHT | wxTOP, border);
        wxBoxSizer* quick = new wxBoxSizer(wxHORIZONTAL);
        m_address = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        m_address->SetHint("e.g. studio-mac.local or 192.168.1.20:1");
        wxButton* quickConnect = new wxButton(panel, wxID_ANY, "Connect");
        quick->Add(m_address, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        quick->Add(quickConnect, 0, wxALIGN_CENTER_VERTICAL);
        outer->Add(quick, 0, wxEXPAND | wxALL, border);

        // Address book.
        outer->Add(new wxStaticText(panel, wxID_ANY, "Saved connections:"), 0, wxLEFT | wxRIGHT, border);
        m_list = new wxListView(panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(520, 260)),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
        m_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(200));
        m_list->AppendColumn("Address", wxLIST_FORMAT_LEFT, FromDIP(180));
        m_list->AppendColumn("Last used", wxLIST_FORMAT_LEFT, FromDIP(120));
        outer->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, border);

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* add = new wxButton(panel, wxID_ANY, "New...");
        m_edit = new wxButton(panel, wxID_ANY, "Edit...");
        m_delete = new wxButton(panel, wxID_ANY, "Delete");
        m_connect = new wxButton(panel, wxID_ANY, "Connect");
        buttons->Add(add, 0, wxRIGHT, gap);
        buttons->Add(m_edit, 0, wxRIGHT, gap);
        buttons->Add(m_delete, 0);
        buttons->AddStretchSpacer();
        buttons->Add(m_connect, 0);
        outer->Add(buttons, 0, wxEXPAND | wxALL, border);

        panel->SetSizer(outer);
        wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(panel, 1, wxEXPAND);
        SetSizerAndFit(frameSizer);
        SetMinSize(GetSize());
        Centre();

        quickConnect->Bind(wxEVT_BUTTON, &ConnectionsFrame::OnQuickConnect, this);
        m_address->Bind(wxEVT_TEXT_ENTER, &ConnectionsFrame::OnQuickConnect, this);
        m_connect->Bind(wxEVT_BUTTON, &ConnectionsFrame::OnConnectSelected, this);
        add->Bind(wxEVT_BUTTON, &ConnectionsFrame::OnNew, this);
        m_edit->Bind(wxEVT_BUTTON, &ConnectionsFrame::OnEdit, this);
        m_delete->Bind(wxEVT_BUTTON, &ConnectionsFrame::OnDelete, this);
        m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ConnectionsFrame::OnItemActivated, this);
        m_list->Bind(wxEVT_LIST_KEY_DOWN, &ConnectionsFrame::OnListKey, this);
        m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
        m_list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { UpdateButtons(); });
        // Viewers update "last used" and their settings; show that when we come back here.
        Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
            event.Skip();
            if (event.GetActive())
            {
                int index = GetSelectedIndex();
                Reload(index >= 0 ? m_connections[index].id : wxString());
            }
        });

        Reload();
        m_address->SetFocus();
    }

    void ConnectionsFrame::Reload(const wxString& selectId)
    {
        m_connections = m_store.LoadAll();
        m_list->DeleteAllItems();
        for (size_t i = 0; i < m_connections.size(); ++i)
        {
            const SavedConnection& connection = m_connections[i];
            long row = m_list->InsertItem(static_cast<long>(i), connection.GetDisplayName());
            m_list->SetItem(row, 1, connection.address);
            m_list->SetItem(row, 2, FormatLastUsed(connection.lastUsed));
            if (!selectId.empty() && connection.id == selectId)
            {
                m_list->Select(row);
                m_list->Focus(row);
            }
        }
        UpdateButtons();
    }

    int ConnectionsFrame::GetSelectedIndex() const
    {
        long selected = m_list->GetFirstSelected();
        return selected >= 0 && selected < static_cast<long>(m_connections.size()) ? static_cast<int>(selected) : -1;
    }

    void ConnectionsFrame::UpdateButtons()
    {
        bool selected = GetSelectedIndex() >= 0;
        m_connect->Enable(selected);
        m_edit->Enable(selected);
        m_delete->Enable(selected);
    }

    void ConnectionsFrame::Connect(const SavedConnection& connection)
    {
        ViewerOptions options;
        std::string error;
        if (!ViewerOptionsFromSaved(connection, m_store, options, error))
        {
            wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
            return;
        }
        m_store.MarkUsed(connection.id);
        (new ViewerFrame(options))->Show();
        Reload(connection.id);
    }

    void ConnectionsFrame::OnQuickConnect(wxCommandEvent&)
    {
        wxString text = m_address->GetValue().Strip(wxString::both);
        Marconi::Address address;
        std::string error;
        if (!Marconi::ParseAddress(text.utf8_string(), address, error))
        {
            wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING, this);
            return;
        }
        // Quick connects are remembered, so the address book doubles as history.
        SavedConnection connection;
        if (!m_store.FindByAddress(text, connection))
        {
            connection.address = text;
            m_store.Save(connection);
        }
        m_address->Clear();
        Connect(connection);
    }

    void ConnectionsFrame::OnConnectSelected(wxCommandEvent&)
    {
        int index = GetSelectedIndex();
        if (index >= 0)
        {
            Connect(m_connections[index]);
        }
    }

    void ConnectionsFrame::OnItemActivated(wxListEvent& event)
    {
        if (event.GetIndex() >= 0 && event.GetIndex() < static_cast<long>(m_connections.size()))
        {
            Connect(m_connections[event.GetIndex()]);
        }
    }

    bool ConnectionsFrame::EditConnection(SavedConnection& connection)
    {
        wxString password;
        if (connection.rememberPassword)
        {
            m_store.LoadPassword(connection.id, password);
        }
        ConnectionDialog dialog(this, connection, password);
        if (dialog.ShowModal() != wxID_OK)
        {
            return false;
        }
        connection = dialog.GetConnection();
        m_store.Save(connection);
        if (connection.rememberPassword && !dialog.GetPassword().empty())
        {
            m_store.SavePassword(connection.id, connection.username, dialog.GetPassword());
        }
        else
        {
            m_store.DeletePassword(connection.id);
        }
        return true;
    }

    void ConnectionsFrame::OnNew(wxCommandEvent&)
    {
        SavedConnection connection;
        connection.address = m_address->GetValue().Strip(wxString::both);
        if (EditConnection(connection))
        {
            Reload(connection.id);
        }
    }

    void ConnectionsFrame::OnEdit(wxCommandEvent&)
    {
        int index = GetSelectedIndex();
        if (index < 0)
        {
            return;
        }
        SavedConnection connection = m_connections[index];
        if (EditConnection(connection))
        {
            Reload(connection.id);
        }
    }

    void ConnectionsFrame::OnDelete(wxCommandEvent&)
    {
        int index = GetSelectedIndex();
        if (index < 0)
        {
            return;
        }
        const SavedConnection& connection = m_connections[index];
        wxMessageDialog confirm(this, wxString::Format("Delete \"%s\"?", connection.GetDisplayName()), "Marconi",
                                wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        confirm.SetYesNoLabels("Delete", "Cancel");
        if (confirm.ShowModal() == wxID_YES)
        {
            m_store.Remove(connection.id);
            Reload();
        }
    }

    void ConnectionsFrame::OnListKey(wxListEvent& event)
    {
        if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_BACK)
        {
            wxCommandEvent dummy;
            OnDelete(dummy);
            return;
        }
        event.Skip();
    }
} // namespace MarconiUI
