// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: quick connect box plus the address book of saved connections

#include "ConnectionsFrame.h"

#include "AppIcon.h"
#include "Appearance.h"
#include "ConnectionDialog.h"
#include "ViewerFrame.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Version.h>

#include <wx/bmpbndl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dcbuffer.h>
#include <wx/imaglist.h>
#include <wx/fontenum.h>
#include <wx/config.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>
#include <wx/tglbtn.h>
#include <wx/utils.h>

#include <cstring>
#include <wx/statbmp.h>
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
        // Keep the native single-line edit at its preferred height, then centre it inside the field.
        wxPanel* CreateTextField(wxWindow* parent, wxTextCtrl*& text, const wxSize& size, const wxString& hintText, long style = 0)
        {
            wxPanel* field = new wxPanel(parent, wxID_ANY, wxDefaultPosition, parent->FromDIP(size),
                                         wxTAB_TRAVERSAL | wxBORDER_NONE);
            field->SetName("MarconiTextField");
            field->SetBackgroundStyle(wxBG_STYLE_PAINT);
            text = new wxTextCtrl(field, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxBORDER_NONE | style);
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, field->FromDIP(10));
            field->SetSizer(row);
            // Native Windows cue banners keep their system colour during live theme changes.
            wxStaticText* hint = new wxStaticText(field, wxID_ANY, hintText);
            hint->SetName("MarconiFieldHint");
            const auto updateHint = [field, text, hint]() {
                hint->SetPosition(wxPoint(field->FromDIP(10),
                    (field->GetClientSize().y - hint->GetBestSize().y) / 2));
                hint->Show(text->IsEmpty() && !text->HasFocus());
                hint->Raise();
            };
            hint->Bind(wxEVT_LEFT_DOWN, [text](wxMouseEvent&) { text->SetFocus(); });
            text->Bind(wxEVT_TEXT, [updateHint](wxCommandEvent& event) { updateHint(); event.Skip(); });
            field->Bind(wxEVT_SIZE, [updateHint](wxSizeEvent& event) { updateHint(); event.Skip(); });
            field->Bind(wxEVT_PAINT, [field, text](wxPaintEvent&) {
                const ThemePalette palette = GetThemePalette();
                wxAutoBufferedPaintDC dc(field);
                dc.SetBackground(wxBrush(palette.background));
                dc.Clear();
                dc.SetPen(wxPen(text->HasFocus() ? palette.accent : palette.border));
                dc.SetBrush(wxBrush(palette.surface));
                const wxSize bounds = field->GetClientSize();
                dc.DrawRoundedRectangle(0, 0, bounds.x, bounds.y, field->FromDIP(4));
            });
            text->Bind(wxEVT_SET_FOCUS, [field, updateHint](wxFocusEvent& event) {
                updateHint(); field->Refresh(); event.Skip();
            });
            text->Bind(wxEVT_KILL_FOCUS, [field, hint, text](wxFocusEvent& event) {
                hint->Show(text->IsEmpty()); field->Refresh(); event.Skip();
            });
            field->Bind(wxEVT_LEFT_DOWN, [text](wxMouseEvent&) { text->SetFocus(); });
            return field;
        }

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
        wxFont bodyFont = GetFont();
        bodyFont.SetPointSize(11);
        SetFont(bodyFont);
        wxPanel* panel = new wxPanel(this);
        const int border = FromDIP(24);
        const int gap = FromDIP(10);
        wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
        auto label = [&](const wxString& text, bool heading = false, bool muted = false) {
            wxStaticText* control = new wxStaticText(panel, wxID_ANY, text);
            if (heading)
            {
                control->SetFont(control->GetFont().Bold().Scaled(1.12f));
            }
            if (muted)
            {
                m_mutedLabels.push_back(control);
            }
            return control;
        };
        auto button = [&](const wxString& text, bool primary = false) {
            wxButton* control = new wxButton(panel, wxID_ANY, text, wxDefaultPosition,
                                             FromDIP(wxSize(-1, 38)));
            if (primary)
            {
                control->SetFont(control->GetFont().Bold());
                control->SetMinSize(FromDIP(wxSize(112, 38)));
                m_primaryButtons.push_back(control);
            }
            return control;
        };
        auto divider = [&]() {
            wxPanel* line = new wxPanel(panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 1)));
            line->SetName("MarconiDivider");
            line->DisableFocusFromKeyboard();
            outer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, border);
        };

        wxBoxSizer* brand = new wxBoxSizer(wxHORIZONTAL);
        const wxBitmapBundle logo = wxBitmapBundle::FromBitmaps(
            wxBitmap(GetAppIcons().GetIcon(wxSize(48, 48))),
            wxBitmap(GetAppIcons().GetIcon(wxSize(96, 96))));
        brand->Add(new wxStaticBitmap(panel, wxID_ANY, logo), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
        wxBoxSizer* wordmark = new wxBoxSizer(wxVERTICAL);
        wxStaticText* title = label("Marconi");
        wxFont brandFont = title->GetFont().Scaled(2.1f).Bold();
        // Windows registers Nebula's individual weights as separate font families.
        if (wxFontEnumerator::IsValidFacename("Nebula Sans Black"))
        {
            brandFont.SetFaceName("Nebula Sans Black");
            brandFont.SetWeight(wxFONTWEIGHT_NORMAL); // The selected face already contains the bold outlines.
        }
        else if (wxFontEnumerator::IsValidFacename("Nebula Sans"))
        {
            brandFont.SetFaceName("Nebula Sans");
            brandFont.SetWeight(wxFONTWEIGHT_HEAVY);
        }
        title->SetFont(brandFont);
        wordmark->Add(title);
        wxStaticText* subtitle = label("Multi-protocol Access and Remote Control Interface", false, true);
        wxFont subtitleFont = subtitle->GetFont();
        subtitleFont.SetWeight(wxFONTWEIGHT_SEMIBOLD);
        if (wxFontEnumerator::IsValidFacename("Nebula Sans Semibold"))
        {
            subtitleFont.SetFaceName("Nebula Sans Semibold");
            subtitleFont.SetWeight(wxFONTWEIGHT_NORMAL); // This face already contains the semibold outlines.
        }
        else if (wxFontEnumerator::IsValidFacename("Nebula Sans"))
        {
            subtitleFont.SetFaceName("Nebula Sans");
        }
        subtitle->SetFont(subtitleFont);
        subtitle->Wrap(FromDIP(450));
        wordmark->Add(subtitle, 0, wxTOP, FromDIP(4));
        brand->Add(wordmark, 1, wxALIGN_CENTER_VERTICAL);
        wxButton* settings = button("Settings");
        brand->Add(settings, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, border);
        outer->Add(brand, 0, wxEXPAND | wxALL, border);
        divider();

        outer->Add(label("Quick connect", true), 0, wxLEFT | wxRIGHT | wxTOP, border);
        wxBoxSizer* quick = new wxBoxSizer(wxHORIZONTAL);
        wxPanel* addressField = CreateTextField(panel, m_address, wxSize(-1, 38),
                                                    "studio-mac.local or 192.168.1.20", wxTE_PROCESS_ENTER);
        m_address->SetName("Remote address");
        wxButton* quickConnect = button("Connect", true);
        quick->Add(addressField, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        quick->Add(quickConnect, 0, wxALIGN_CENTER_VERTICAL);
        wxBoxSizer* quickInset = new wxBoxSizer(wxVERTICAL);
        quickInset->Add(quick, 0, wxEXPAND | wxTOP, gap);
        outer->Add(quickInset, 0, wxEXPAND | wxLEFT | wxRIGHT, border);
        wxBoxSizer* hintInset = new wxBoxSizer(wxVERTICAL);
        hintInset->Add(label("Hostname, IP address, or VNC URL. Use host::port for a specific port.", false, true),
                       0, wxTOP, FromDIP(8));
        outer->Add(hintInset, 0, wxLEFT | wxRIGHT, border);
        outer->AddSpacer(border);
        divider();

        wxBoxSizer* savedHeading = new wxBoxSizer(wxHORIZONTAL);
        savedHeading->Add(label("Saved connections", true), 1, wxALIGN_CENTER_VERTICAL);
        wxPanel* searchField = CreateTextField(panel, m_search, wxSize(250, 34), "Search connections...");
        m_search->SetName("Search saved connections");
        savedHeading->Add(searchField, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
        outer->Add(savedHeading, 0, wxEXPAND | wxALL, border);

        // Native Windows header backgrounds retain their startup theme. Keep this strip in wx so
        // both colours change immediately, while retaining the native list for rows and selection.
        wxPanel* tableHeader = new wxPanel(panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 36)));
        tableHeader->SetName("MarconiTableHeader");
        tableHeader->DisableFocusFromKeyboard();
        tableHeader->SetBackgroundStyle(wxBG_STYLE_PAINT);
        std::vector<wxStaticText*> headings;
        for (const wxString& name : {wxString("Name"), wxString("Address"), wxString("Last used")})
        {
            wxStaticText* heading = new wxStaticText(tableHeader, wxID_ANY, name);
            heading->SetName("MarconiHeaderLabel");
            heading->SetFont(heading->GetFont().Bold());
            headings.push_back(heading);
        }
        tableHeader->Bind(wxEVT_SIZE, [tableHeader, headings](wxSizeEvent& event) {
            const wxSize bounds = tableHeader->GetClientSize();
            const int starts[] = {0, bounds.x * 32 / 100, bounds.x * 74 / 100};
            for (int i = 0; i < 3; ++i)
            {
                headings[i]->SetPosition(wxPoint(starts[i] + tableHeader->FromDIP(7),
                    (bounds.y - headings[i]->GetBestSize().y) / 2));
            }
            event.Skip();
        });
        tableHeader->Bind(wxEVT_PAINT, [tableHeader](wxPaintEvent&) {
            const ThemePalette palette = GetThemePalette();
            wxAutoBufferedPaintDC dc(tableHeader);
            dc.SetBackground(wxBrush(palette.surface));
            dc.Clear();
            dc.SetPen(wxPen(palette.border));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(tableHeader->GetClientRect());
        });
        outer->Add(tableHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, border);

        m_list = new wxListView(panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(650, 180)),
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_SIMPLE);
        m_list->SetName("Saved connections");
        m_list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(205));
        m_list->AppendColumn("Address", wxLIST_FORMAT_LEFT, FromDIP(280));
        m_list->AppendColumn("Last used", wxLIST_FORMAT_LEFT, FromDIP(150));
        // A transparent image sets a comfortable row height without custom-drawing the accessible native list.
        wxImage blank(1, FromDIP(34));
        blank.InitAlpha();
        memset(blank.GetAlpha(), 0, blank.GetHeight());
        wxImageList* rowImages = new wxImageList(1, blank.GetHeight(), true);
        rowImages->Add(wxBitmap(blank));
        m_list->AssignImageList(rowImages, wxIMAGE_LIST_SMALL);
        outer->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, border);
        m_empty = label("", false, true);
        m_empty->Hide();
        outer->Add(m_empty, 0, wxLEFT | wxRIGHT | wxTOP, border);

        wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
        wxButton* add = button("New connection");
        m_edit = button("Edit");
        m_delete = button("Delete");
        m_connect = button("Connect", true);
        buttons->Add(add, 0, wxRIGHT, gap);
        buttons->Add(m_edit, 0, wxRIGHT, gap);
        buttons->Add(m_delete, 0);
        buttons->AddStretchSpacer();
        buttons->Add(m_connect, 0);
        outer->Add(buttons, 0, wxEXPAND | wxALL, border);
        divider();

        wxBoxSizer* footer = new wxBoxSizer(wxHORIZONTAL);
        m_count = label("", false, true);
        footer->Add(m_count, 1, wxALIGN_CENTER_VERTICAL);
        footer->Add(label("Appearance", false, true), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
        const wxString modes[] = {"System", "Light", "Dark"};
        for (int i = 0; i < 3; ++i)
        {
            m_themeButtons[i] = new wxToggleButton(panel, wxID_ANY, modes[i], wxDefaultPosition,
                                                    FromDIP(wxSize(70, 34)));
            footer->Add(m_themeButtons[i], 0);
            m_themeButtons[i]->Bind(wxEVT_TOGGLEBUTTON, [this, i](wxCommandEvent&) { ChooseAppearance(i); });
        }
        outer->Add(footer, 0, wxEXPAND | wxALL, border);
        m_notice = label("", false, true);
        m_notice->Hide();
        m_notice->SetFont(m_notice->GetFont().Scaled(0.9f));
        outer->Add(m_notice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);

        panel->SetSizer(outer);
        wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(panel, 1, wxEXPAND);
        SetSizer(frameSizer);
        SetClientSize(FromDIP(wxSize(740, 800)));
        SetMinClientSize(FromDIP(wxSize(720, 800)));
        Centre();
        settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowSettings(); });
        m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) { Reload(); event.Skip(); });
        Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
            event.Skip();
            ApplyAppearance();
        });
        m_list->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            const int width = m_list->GetClientSize().x;
            m_list->SetColumnWidth(0, width * 32 / 100);
            m_list->SetColumnWidth(1, width * 42 / 100);
            m_list->SetColumnWidth(2, width - width * 32 / 100 - width * 42 / 100 - FromDIP(4));
        });

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
        ApplyAppearance();
        m_address->SetFocus();
    }

    void ConnectionsFrame::Reload(const wxString& selectId)
    {
        wxString selected = selectId;
        const int previous = GetSelectedIndex();
        if (selected.empty() && previous >= 0)
        {
            selected = m_connections[previous].id;
        }
        const auto all = m_store.LoadAll();
        const wxString query = m_search->GetValue().Strip(wxString::both).Lower();
        m_list->Freeze();
        m_list->DeleteAllItems();
        m_connections.clear();
        for (const SavedConnection& connection : all)
        {
            if (!query.empty() && !connection.GetDisplayName().Lower().Contains(query) &&
                !connection.address.Lower().Contains(query))
            {
                continue;
            }
            const long row = m_list->InsertItem(static_cast<long>(m_connections.size()), connection.GetDisplayName(), 0);
            m_connections.push_back(connection);
            m_list->SetItem(row, 1, connection.address);
            m_list->SetItem(row, 2, FormatLastUsed(connection.lastUsed));
            if (!selected.empty() && connection.id == selected)
            {
                m_list->Select(row);
                m_list->Focus(row);
            }
        }
        m_list->Thaw();
        m_count->SetLabel(query.empty() ? wxString::Format("%zu saved connection%s", all.size(),
            all.size() == 1 ? "" : "s") : wxString::Format("%zu of %zu connections", m_connections.size(), all.size()));
        m_empty->SetLabel(all.empty() ? "Your connections will appear here. Connect above or add a new connection." :
            "No matching connections. Try a different name or address.");
        m_empty->Show(m_connections.empty());
        Layout();
        UpdateButtons();
    }

    void ConnectionsFrame::ApplyAppearance()
    {
        StyleWindow(this);
        const ThemePalette palette = GetThemePalette();
        for (wxStaticText* text : m_mutedLabels)
        {
            text->SetForegroundColour(palette.muted);
        }
        for (wxButton* button : m_primaryButtons)
        {
            button->SetBackgroundColour(palette.accent);
            button->SetForegroundColour(*wxWHITE);
        }
        for (int i = 0; i < 3; ++i)
        {
            const bool selected = i == static_cast<int>(GetThemeMode());
            m_themeButtons[i]->SetValue(selected);
            m_themeButtons[i]->SetBackgroundColour(selected ? palette.selection : palette.surface);
            m_themeButtons[i]->SetForegroundColour(palette.text);
        }
        m_notice->SetLabel(GetAppearanceNotice());
        m_notice->Show(!GetAppearanceNotice().empty());
        Layout();
        Refresh();
    }

    void ConnectionsFrame::ChooseAppearance(int selection)
    {
        if (!SaveThemeMode(static_cast<ThemeMode>(selection)))
        {
            wxMessageBox("The appearance preference could not be saved. Please try again.", "Marconi",
                         wxOK | wxICON_WARNING, this);
        }
        ApplyAppearance();
    }

    void ConnectionsFrame::ShowSettings()
    {
        wxDialog dialog(this, wxID_ANY, "Settings", wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        const int space = dialog.FromDIP(20);
        wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
        wxStaticText* heading = new wxStaticText(&dialog, wxID_ANY, "Make Marconi yours");
        heading->SetFont(heading->GetFont().Bold().Scaled(1.3f));
        layout->Add(heading, 0, wxALL, space);
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(&dialog, wxID_ANY, "Appearance"), 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, space);
        wxChoice* appearance = new wxChoice(&dialog, wxID_ANY);
        appearance->Append("System");
        appearance->Append("Light");
        appearance->Append("Dark");
        appearance->SetSelection(static_cast<int>(GetThemeMode()));
        row->Add(appearance, 1);
        layout->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, space);
        wxStaticText* help = new wxStaticText(&dialog, wxID_ANY,
            "System follows your device's appearance. Native menus and title bars may need a restart.");
        help->Wrap(dialog.FromDIP(380));
        layout->Add(help, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, space);
        wxButton* logs = new wxButton(&dialog, wxID_ANY, "Open log folder");
        logs->Bind(wxEVT_BUTTON, [&dialog](wxCommandEvent&) {
            const wxString path = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs";
            if (!wxLaunchDefaultApplication(path))
            {
                wxMessageBox("Could not open the log folder: " + path, "Marconi", wxOK | wxICON_WARNING, &dialog);
            }
        });
        layout->Add(logs, 0, wxLEFT | wxRIGHT | wxBOTTOM, space);
        layout->Add(new wxStaticText(&dialog, wxID_ANY,
            "Marconi " + wxString::FromUTF8(Marconi::GetVersionString()) + " / Red Marmoset Studios"),
            0, wxLEFT | wxRIGHT | wxBOTTOM, space);
        layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, space);
        dialog.SetSizerAndFit(layout);
        StyleWindow(&dialog);
        dialog.CentreOnParent();
        if (dialog.ShowModal() == wxID_OK)
        {
            ChooseAppearance(appearance->GetSelection());
        }
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
        wxString oldAddress = connection.address;
        connection = dialog.GetConnection();
        if (connection.address != oldAddress)
        {
            connection.detected.clear(); // a different machine, or a different protocol: probe again
        }
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
