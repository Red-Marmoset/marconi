// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Add / edit a saved connection

#include "ConnectionDialog.h"
#include "Appearance.h"

#include "SystemKeys.h"

#include <MarconiCore/Zoom.h>
#include <MarconiCore/Address.h>
#include <MarconiCore/Net/WakeOnLan.h>
#include <MarconiCore/Quality.h>

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
        addRow("Shown in list as:", m_name);
        m_address = new wxTextCtrl(this, wxID_ANY, connection.address, wxDefaultPosition, fieldSize);
        m_address->SetHint("host, host:display or host::port");
        addRow("Address:", m_address);
        m_username = new wxTextCtrl(this, wxID_ANY, connection.username, wxDefaultPosition, fieldSize);
        m_username->SetHint("Macs: your account or full name");
        addRow("Login name:", m_username);
        m_password = new wxTextCtrl(this, wxID_ANY, password, wxDefaultPosition, fieldSize, wxTE_PASSWORD);
        m_password->SetHint("with a login name: that account's password");
        addRow("Password:", m_password);
        grid->AddSpacer(0);
        m_rememberPassword = new wxCheckBox(this, wxID_ANY, "Remember password in the system keychain");
        m_rememberPassword->SetValue(connection.rememberPassword);
        grid->Add(m_rememberPassword);
        m_mac = new wxTextCtrl(this, wxID_ANY, connection.macAddress, wxDefaultPosition, fieldSize);
        m_mac->SetHint("optional, e.g. AA:BB:CC:DD:EE:FF, to wake it from sleep");
        addRow("Hardware address:", m_mac);
        m_notes = new wxTextCtrl(this, wxID_ANY, connection.notes, wxDefaultPosition, FromDIP(wxSize(260, 56)),
                                 wxTE_MULTILINE);
        m_notes->SetHint("optional, e.g. the PC under the desk; VNC on port 5901");
        addRow("Notes:", m_notes);
        grid->AddSpacer(0);
        m_favourite = new wxCheckBox(this, wxID_ANY, "Favourite (listed first)");
        m_favourite->SetValue(connection.favourite);
        grid->Add(m_favourite);
        outer->Add(grid, 0, wxEXPAND | wxALL, FromDIP(20));

        wxStaticBoxSizer* view = new wxStaticBoxSizer(wxVERTICAL, this, "Viewer");
        wxBoxSizer* scaleRow = new wxBoxSizer(wxHORIZONTAL);
        scaleRow->Add(new wxStaticText(view->GetStaticBox(), wxID_ANY, "Size:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        m_scale = new wxChoice(view->GetStaticBox(), wxID_ANY);
        m_scale->Append("Scale to fit the window");
        for (size_t i = 0; i < Marconi::Zoom::kLevelCount; ++i)
        {
            int level = Marconi::Zoom::kLevels[i];
            m_scale->Append(level == 100 ? wxString("Actual size (100%)") : wxString::Format("%d%%", level));
            if (connection.scaleMode == ScaleMode::Fixed && connection.zoomPercent == level)
            {
                m_scale->SetSelection(static_cast<int>(i) + 1);
            }
        }
        if (m_scale->GetSelection() == wxNOT_FOUND)
        {
            m_scale->SetSelection(0);
        }
        scaleRow->Add(m_scale, 1);
        view->Add(scaleRow, 0, wxEXPAND | wxALL, gap);
        wxBoxSizer* qualityRow = new wxBoxSizer(wxHORIZONTAL);
        qualityRow->Add(new wxStaticText(view->GetStaticBox(), wxID_ANY, "Picture:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        m_quality = new wxChoice(view->GetStaticBox(), wxID_ANY);
        m_quality->Append("Best (no loss of detail)");
        m_quality->Append("Balanced");
        m_quality->Append("Low bandwidth (more compression, less detail)");
        switch (Marconi::QualityFromName(connection.quality.utf8_string()))
        {
        case Marconi::PictureQuality::Best: m_quality->SetSelection(0); break;
        case Marconi::PictureQuality::LowBandwidth: m_quality->SetSelection(2); break;
        default: m_quality->SetSelection(1); break;
        }
        qualityRow->Add(m_quality, 1);
        view->Add(qualityRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, gap);
        m_viewOnly = new wxCheckBox(view->GetStaticBox(), wxID_ANY, "View only (don't send keyboard or mouse)");
        m_viewOnly->SetValue(connection.viewOnly);
        view->Add(m_viewOnly, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        m_clipboard = new wxCheckBox(view->GetStaticBox(), wxID_ANY, "Share the clipboard");
        m_clipboard->SetValue(connection.clipboard);
        view->Add(m_clipboard, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        m_systemKeys = new wxCheckBox(view->GetStaticBox(), wxID_ANY,
                                      "Send system shortcuts (Alt+Tab, the Windows key, Cmd+Tab) to the other computer");
        m_systemKeys->SetValue(connection.systemKeys && SystemKeyCapture::IsSupported());
        if (!SystemKeyCapture::IsSupported())
        {
            m_systemKeys->Disable();
            m_systemKeys->SetToolTip(wxString::FromUTF8(SystemKeyCapture::GetUnsupportedReason()));
        }
        view->Add(m_systemKeys, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        m_lock = new wxCheckBox(view->GetStaticBox(), wxID_ANY, "Lock the other computer's screen when I disconnect");
        m_lock->SetValue(connection.lockOnDisconnect);
        view->Add(m_lock, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
        outer->Add(view, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

        outer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(20));
        SetSizerAndFit(outer);
        StyleWindow(this);
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
        Marconi::Net::MacAddress mac;
        wxString macText = m_mac->GetValue().Strip(wxString::both);
        if (!macText.empty() && !Marconi::Net::ParseMacAddress(macText.utf8_string(), mac))
        {
            wxMessageBox("The hardware address should be six pairs of letters or digits, like AA:BB:CC:DD:EE:FF. "
                         "On Windows, \"ipconfig /all\" on that computer shows it as the Physical Address.",
                         GetTitle(), wxOK | wxICON_WARNING, this);
            m_mac->SetFocus();
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
        int selection = m_scale->GetSelection();
        connection.scaleMode = selection > 0 ? ScaleMode::Fixed : ScaleMode::Fit;
        if (selection > 0)
        {
            connection.zoomPercent = Marconi::Zoom::kLevels[selection - 1];
        }
        connection.viewOnly = m_viewOnly->GetValue();
        connection.clipboard = m_clipboard->GetValue();
        connection.systemKeys = m_systemKeys->GetValue();
        static const Marconi::PictureQuality kQualities[] = {Marconi::PictureQuality::Best, Marconi::PictureQuality::Balanced,
                                                              Marconi::PictureQuality::LowBandwidth};
        int quality = m_quality->GetSelection();
        connection.quality = Marconi::QualityName(kQualities[quality >= 0 && quality < 3 ? quality : 1]);
        connection.lockOnDisconnect = m_lock->GetValue();
        connection.favourite = m_favourite->GetValue();
        Marconi::Net::MacAddress mac;
        wxString macText = m_mac->GetValue().Strip(wxString::both);
        connection.macAddress = Marconi::Net::ParseMacAddress(macText.utf8_string(), mac)
                                    ? wxString(Marconi::Net::FormatMacAddress(mac))
                                    : wxString();
        connection.notes = m_notes->GetValue().Strip(wxString::both);
        return connection;
    }

    wxString ConnectionDialog::GetPassword() const
    {
        return m_rememberPassword->GetValue() ? m_password->GetValue() : wxString();
    }
} // namespace MarconiUI
