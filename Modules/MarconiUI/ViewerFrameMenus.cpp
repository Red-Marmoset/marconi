// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: the toolbar, the F8 menu, and what each command does

#include "ViewerFrame.h"

#include "ClipboardFiles.h"
#include "Controls.h"
#include "ViewerToolbarArt.h"
#include "FilePanel.h"
#include "ViewerCommands.h"

#include <MarconiCore/Keysyms.h>
#include <MarconiCore/Zoom.h>

#include <Log/Log.h>

#include <wx/aui/auibar.h>
#include <wx/config.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <memory>

namespace MarconiUI
{
    void ViewerFrame::CreateToolbar()
    {
        // Text-only buttons in a panel that docks to any edge of the window or floats (F9 shows and hides it).
        m_toolBar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxAUI_TB_TEXT | wxAUI_TB_HORZ_LAYOUT | wxAUI_TB_GRIPPER | wxAUI_TB_OVERFLOW);
        m_toolBar->SetArtProvider(new ViewerToolbarArt());
        wxFont toolbarFont = GetFont();
        toolbarFont.SetPointSize(10);
        m_toolBar->SetFont(HeadingFont(toolbarFont));
        m_toolBar->SetMargins(FromDIP(8), FromDIP(6));
        m_toolBar->SetToolPacking(FromDIP(4));
        m_toolBar->SetToolBorderPadding(0);
        m_toolBar->AddTool(ID_SCALE_FIT, "Scale to Fit", wxNullBitmap,
                           "Scale the remote desktop to fit the window (otherwise show it at the chosen zoom level)",
                           wxITEM_CHECK);
        m_toolBar->AddTool(ID_ZOOM_IN, "Zoom In", wxNullBitmap, "Zoom in (Ctrl and +, or Ctrl and the mouse wheel)");
        m_toolBar->AddTool(ID_ZOOM_OUT, "Zoom Out", wxNullBitmap, "Zoom out (Ctrl and -, or Ctrl and the mouse wheel)");
        m_toolBar->AddTool(ID_FULL_SCREEN, "Full Screen", wxNullBitmap,
                           "Full screen (move the pointer to the top edge for the controls, or press F8)");
        m_toolBar->AddTool(ID_FILES, "Files", wxNullBitmap, "Copy files to and from the remote computer (F10)",
                           wxITEM_CHECK);
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_VIEW_ONLY, "View Only", wxNullBitmap, "Watch without sending mouse or keyboard input",
                           wxITEM_CHECK);
        m_toolBar->AddTool(ID_CLIPBOARD, "Clipboard", wxNullBitmap, "Share copied text with the remote computer",
                           wxITEM_CHECK);
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_SEND_CTRL_ALT_DEL, "Ctrl+Alt+Del", wxNullBitmap, "Send Ctrl+Alt+Del");
        m_toolBar->AddTool(ID_SEND_KEYS, "Send Keys", wxNullBitmap,
                           "Send a shortcut the remote computer can't otherwise receive (Alt+Tab, the Windows key...)");
        m_toolBar->AddTool(ID_SCREENSHOT, "Screenshot", wxNullBitmap, "Save or copy a picture of the remote desktop");
        // Marconi keeps F8, F9 and F10 for itself, so these send them to the remote computer.
        m_toolBar->AddTool(ID_SEND_F8, "F8", wxNullBitmap, "Send F8 to the remote computer");
        m_toolBar->AddTool(ID_SEND_F9, "F9", wxNullBitmap, "Send F9 to the remote computer");
        m_toolBar->AddTool(ID_SEND_F10, "F10", wxNullBitmap, "Send F10 to the remote computer");
        m_toolBar->AddTool(ID_REFRESH, "Refresh", wxNullBitmap, "Ask the server for a full screen update");
        m_toolBar->AddSeparator();
        m_toolBar->AddTool(ID_DISCONNECT, "Disconnect", wxNullBitmap, "Close this connection");
        m_toolBar->Realize();
        m_aui.AddPane(m_toolBar, wxAuiPaneInfo()
                                     .Name("Toolbar")
                                     .Caption("Toolbar")
                                     .ToolbarPane()
                                     .Top()
                                     .Floatable(true)
                                     .Dockable(true)
                                     .CloseButton(false));
    }

    wxMenu* ViewerFrame::CreateMenu() const
    {
        wxMenu* menu = new wxMenu();
        wxMenu* zoom = new wxMenu();
        zoom->AppendRadioItem(ID_SCALE_FIT, "Scale to &Fit the Window");
        for (size_t i = 0; i < Marconi::Zoom::kLevelCount; ++i)
        {
            int level = Marconi::Zoom::kLevels[i];
            wxString label = wxString::Format("%d%%", level);
            if (level == 100)
            {
                label = "&Actual Size (100%)\tCtrl+0";
            }
            zoom->AppendRadioItem(ID_ZOOM_LEVEL_FIRST + static_cast<int>(i), label);
        }
        zoom->AppendSeparator();
        zoom->Append(ID_ZOOM_IN, "Zoom &In\tCtrl++");
        zoom->Append(ID_ZOOM_OUT, "Zoom &Out\tCtrl+-");
        menu->AppendSubMenu(zoom, "&Zoom");
        if (m_screens.size() > 1)
        {
            wxMenu* displays = new wxMenu();
            displays->AppendRadioItem(ID_DISPLAY_ALL, "&All Displays");
            bool chosen = false;
            for (size_t i = 0; i < m_screens.size() && i < 16; ++i)
            {
                const Marconi::Rfb::Screen& screen = m_screens[i];
                displays->AppendRadioItem(ID_DISPLAY_FIRST + static_cast<int>(i),
                                          wxString::Format("Display &%zu (%d x %d)", i + 1, screen.width, screen.height));
                if (static_cast<long>(screen.id) == m_displayId)
                {
                    displays->Check(ID_DISPLAY_FIRST + static_cast<int>(i), true);
                    chosen = true;
                }
            }
            if (!chosen)
            {
                displays->Check(ID_DISPLAY_ALL, true);
            }
            menu->AppendSubMenu(displays, "&Display");
        }
        wxMenu* quality = new wxMenu();
        quality->AppendRadioItem(ID_QUALITY_FIRST, "&Best (no loss of detail)");
        quality->AppendRadioItem(ID_QUALITY_FIRST + 1, "&Balanced");
        quality->AppendRadioItem(ID_QUALITY_FIRST + 2, "&Low Bandwidth");
        quality->Check(ID_QUALITY_FIRST + (m_quality == Marconi::PictureQuality::Best ? 0
                                           : m_quality == Marconi::PictureQuality::LowBandwidth ? 2 : 1), true);
        menu->AppendSubMenu(quality, "Picture &Quality");
        menu->AppendCheckItem(ID_MATCH_WINDOW_SIZE, "&Match the Other Computer's Screen to This Window",
                              "Asks the other computer to change its screen size to fit this window exactly");
        menu->Check(ID_MATCH_WINDOW_SIZE, m_matchWindowSize);
        menu->Enable(ID_MATCH_WINDOW_SIZE, m_connected && m_session && m_session->SupportsDesktopResize());
        menu->AppendCheckItem(ID_FULL_SCREEN, "F&ull Screen");
        menu->AppendCheckItem(ID_VIEW_ONLY, "&View Only");
        menu->AppendCheckItem(ID_CLIPBOARD, "Share C&lipboard");
        menu->Append(ID_SEND_CLIPBOARD, "Send Clipboard to the Other Computer");
        menu->Append(ID_GET_CLIPBOARD, "Get Clipboard from the Other Computer");
        menu->Append(ID_PASTE_FILES, "Paste Files onto the Other Computer's Desktop\tCtrl+V");
        menu->AppendSeparator();
        menu->AppendSubMenu(CreateSendKeysMenu(), "Send &Keys");
        menu->AppendSubMenu(CreateScreenshotMenu(), "Screens&hot");
        wxString systemKeysLabel = "Send System Shortcuts (Alt+Tab, Windows Key) to the Other Computer";
#if defined(__WXOSX__)
        systemKeysLabel = "Send System Shortcuts (Cmd+Tab, Cmd+Space) to the Other Computer";
#endif
        menu->AppendCheckItem(ID_SYSTEM_KEYS, systemKeysLabel,
                              SystemKeyCapture::IsSupported()
                                  ? wxString()
                                  : wxString::FromUTF8(SystemKeyCapture::GetUnsupportedReason()));
        menu->Append(ID_REFRESH, "&Refresh Screen");
        menu->AppendSeparator();
        menu->AppendCheckItem(ID_SHOW_TOOLBAR, "Show &Toolbar\tF9");
        menu->AppendCheckItem(ID_FILES, "Transfer &Files\tF10");
        menu->AppendSeparator();
        menu->Append(ID_DISCONNECT, "Dis&connect");

        if (m_canvas->GetScaleMode() == ScaleMode::Fit)
        {
            zoom->Check(ID_SCALE_FIT, true);
        }
        else
        {
            for (size_t i = 0; i < Marconi::Zoom::kLevelCount; ++i)
            {
                if (Marconi::Zoom::kLevels[i] == m_canvas->GetZoomPercent())
                {
                    zoom->Check(ID_ZOOM_LEVEL_FIRST + static_cast<int>(i), true);
                }
            }
        }
        menu->Check(ID_FULL_SCREEN, IsFullScreen());
        menu->Check(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        menu->Check(ID_CLIPBOARD, m_clipboard.IsEnabled());
        menu->Check(ID_SYSTEM_KEYS, m_sendSystemKeys);
        menu->Enable(ID_SYSTEM_KEYS, SystemKeyCapture::IsSupported());
        menu->Check(ID_SHOW_TOOLBAR, m_toolBar->IsShown()); // AUI hides the toolbar window with its pane
        menu->Check(ID_FILES, m_files->IsShown());
        for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_ALT_TAB, ID_SEND_ALT_F4, ID_SEND_WINDOWS_KEY, ID_SEND_CTRL_ESC,
                       ID_SEND_CTRL_SHIFT_ESC, ID_SEND_PRINT_SCREEN, ID_SEND_CMD_TAB, ID_SEND_F8, ID_SEND_F9,
                       ID_SEND_F10, ID_REFRESH})
        {
            menu->Enable(id, m_connected && (id == ID_REFRESH || !m_canvas->IsViewOnly()));
        }
        menu->Enable(ID_SEND_CLIPBOARD, m_connected && !m_canvas->IsViewOnly());
        menu->Enable(ID_GET_CLIPBOARD, m_connected);
        menu->Enable(ID_PASTE_FILES, m_connected && ClipboardHasFiles());
        return menu;
    }

    wxMenu* ViewerFrame::CreateSendKeysMenu() const
    {
        // Shortcuts the local system would act on itself, so they can't simply be typed.
        wxMenu* keys = new wxMenu();
        keys->Append(ID_SEND_CTRL_ALT_DEL, "Ctrl+Alt+&Del");
        keys->Append(ID_SEND_ALT_TAB, "Alt+&Tab");
        keys->Append(ID_SEND_ALT_F4, "Alt+F&4");
        keys->Append(ID_SEND_WINDOWS_KEY, "&Windows Key");
        keys->Append(ID_SEND_CTRL_ESC, "Ctrl+&Esc");
        keys->Append(ID_SEND_CTRL_SHIFT_ESC, "Ctrl+&Shift+Esc");
        keys->Append(ID_SEND_PRINT_SCREEN, "&Print Screen");
        keys->Append(ID_SEND_CMD_TAB, "&Cmd+Tab (Mac)");
        keys->AppendSeparator();
        // Marconi keeps F8, F9 and F10 for itself, so these send them to the remote computer.
        keys->Append(ID_SEND_F8, "F&8");
        keys->Append(ID_SEND_F9, "F&9");
        keys->Append(ID_SEND_F10, "F1&0");
        return keys;
    }

    void ViewerFrame::UpdateSystemKeyCapture()
    {
        bool wanted = m_sendSystemKeys && m_connected && m_active && !m_canvas->IsViewOnly() &&
                      SystemKeyCapture::IsSupported() && !m_options.headless;
        if (!wanted)
        {
            if (m_systemKeys)
            {
                m_systemKeys->Stop();
            }
            return;
        }
        if (!m_systemKeys)
        {
            m_systemKeys = SystemKeyCapture::Create([this](uint32_t keysym, bool down) {
                if (m_connected && !m_canvas->IsViewOnly() && m_session)
                {
                    m_session->SendKeyEvent(keysym, down);
                }
            });
        }
        if (!m_systemKeys || m_systemKeys->IsActive())
        {
            return;
        }
        std::string problem;
        if (!m_systemKeys->Start(problem))
        {
            LOG_INFO("Viewer", "Can't send system shortcuts: %s", problem.c_str());
            m_sendSystemKeys = false;
            wxMessageBox("Marconi can't send system shortcuts to the other computer: " + wxString::FromUTF8(problem),
                         "Marconi", wxOK | wxICON_INFORMATION, this);
        }
    }

    void ViewerFrame::ShowMenu()
    {
        std::unique_ptr<wxMenu> menu(CreateMenu());
        m_canvas->ReleaseInput();
        PopupMenu(menu.get(), m_canvas->GetPosition() + wxPoint(FromDIP(8), FromDIP(8)));
        m_canvas->SetFocus();
    }

    void ViewerFrame::OnCommand(wxCommandEvent& event)
    {
        // A toolbar click may have taken the keyboard focus; give it back to the desktop afterwards.
        CallAfter([this]() { m_canvas->SetFocus(); });
        switch (event.GetId())
        {
        case ID_SCALE_FIT:
            // The toolbar button toggles; the menu's radio item always selects Fit.
            m_canvas->SetScaleMode(event.GetEventType() == wxEVT_TOOL && !event.IsChecked() ? ScaleMode::Fixed
                                                                                            : ScaleMode::Fit);
            break;
        case ID_ZOOM_IN:
        case ID_ZOOM_OUT:
        {
            wxSize client = m_canvas->GetClientSize();
            m_canvas->ZoomStep(event.GetId() == ID_ZOOM_IN, wxPoint(client.x / 2, client.y / 2));
            break;
        }
        case ID_FULL_SCREEN:
            ToggleFullScreen();
            break;
        case ID_VIEW_ONLY:
            m_canvas->SetViewOnly(!m_canvas->IsViewOnly());
            UpdateSystemKeyCapture();
            break;
        case ID_CLIPBOARD:
            m_clipboard.SetEnabled(!m_clipboard.IsEnabled());
            if (m_clipboard.IsEnabled())
            {
                m_clipboard.Reset();
                SyncClipboardToServer();
            }
            break;
        case ID_SEND_CLIPBOARD:
            m_clipboard.SendNow();
            return; // keep the status bar message; the automatic send below would repeat it
        case ID_GET_CLIPBOARD:
            m_clipboard.GetNow();
            return;
        case ID_PASTE_FILES:
            PasteFilesFromClipboard();
            return;
        case ID_SEND_CTRL_ALT_DEL:
            SendKeyCombination({Marconi::Keysym::Control_L, Marconi::Keysym::Alt_L, Marconi::Keysym::Delete});
            break;
        case ID_SEND_ALT_TAB:
            SendKeyCombination({Marconi::Keysym::Alt_L, Marconi::Keysym::Tab});
            break;
        case ID_SEND_ALT_F4:
            SendKeyCombination({Marconi::Keysym::Alt_L, Marconi::Keysym::F1 + 3});
            break;
        case ID_SEND_WINDOWS_KEY:
            SendKeyCombination({Marconi::Keysym::Super_L});
            break;
        case ID_SEND_CTRL_ESC:
            SendKeyCombination({Marconi::Keysym::Control_L, Marconi::Keysym::Escape});
            break;
        case ID_SEND_CTRL_SHIFT_ESC:
            SendKeyCombination({Marconi::Keysym::Control_L, Marconi::Keysym::Shift_L, Marconi::Keysym::Escape});
            break;
        case ID_SEND_PRINT_SCREEN:
            SendKeyCombination({Marconi::Keysym::Print});
            break;
        case ID_SEND_CMD_TAB:
            SendKeyCombination({Marconi::Keysym::Super_L, Marconi::Keysym::Tab});
            break;
        case ID_SEND_KEYS:
        {
            std::unique_ptr<wxMenu> keys(CreateSendKeysMenu());
            for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_ALT_TAB, ID_SEND_ALT_F4, ID_SEND_WINDOWS_KEY, ID_SEND_CTRL_ESC,
                           ID_SEND_CTRL_SHIFT_ESC, ID_SEND_PRINT_SCREEN, ID_SEND_CMD_TAB, ID_SEND_F8, ID_SEND_F9,
                           ID_SEND_F10})
            {
                keys->Enable(id, m_connected && !m_canvas->IsViewOnly());
            }
            m_canvas->ReleaseInput();
            if (event.GetEventObject() == m_fullScreenBar)
            {
                m_fullScreenBar->PopupMenu(keys.get(), m_fullScreenBar->ScreenToClient(wxGetMousePosition()));
            }
            else
            {
                wxRect tool = m_toolBar->GetToolRect(ID_SEND_KEYS);
                m_toolBar->PopupMenu(keys.get(), tool.GetBottomLeft());
            }
            break;
        }
        case ID_SYSTEM_KEYS:
            m_sendSystemKeys = !m_sendSystemKeys;
            UpdateSystemKeyCapture();
            break;
        case ID_SCREENSHOT:
        {
            std::unique_ptr<wxMenu> screenshot(CreateScreenshotMenu());
            m_canvas->ReleaseInput();
            wxRect tool = m_toolBar->GetToolRect(ID_SCREENSHOT);
            m_toolBar->PopupMenu(screenshot.get(), tool.GetBottomLeft());
            break;
        }
        case ID_SCREENSHOT_SAVE:
            SaveScreenshot();
            return; // keep the status bar message
        case ID_SCREENSHOT_COPY:
            CopyScreenshot();
            return;
        case ID_SCREENSHOT_POINTER:
            wxConfigBase::Get()->Write("/Screenshots/IncludePointer", !IncludePointerInScreenshots());
            wxConfigBase::Get()->Flush();
            break;
        case ID_SEND_F8:
            SendKeyCombination({Marconi::Keysym::F1 + 7});
            break;
        case ID_SEND_F9:
            SendKeyCombination({Marconi::Keysym::F1 + 8});
            break;
        case ID_SEND_F10:
            SendKeyCombination({Marconi::Keysym::F1 + 9});
            break;
        case ID_SHOW_TOOLBAR:
            ToggleToolbar();
            break;
        case ID_SHOW_MENU:
            ShowMenu();
            return;
        case ID_FILES:
            ToggleFiles();
            break;
        case ID_REFRESH:
            if (m_session)
            {
                m_session->RequestFullUpdate();
            }
            break;
        case ID_DISCONNECT:
            Close();
            return;
        case ID_DISPLAY_ALL:
            m_displayId = 0;
            ApplyDisplayChoice();
            break;
        case ID_MATCH_WINDOW_SIZE:
            m_matchWindowSize = !m_matchWindowSize;
            if (m_matchWindowSize)
            {
                RequestMatchingSize();
            }
            break;
        default:
            if (event.GetId() >= ID_ZOOM_LEVEL_FIRST && event.GetId() <= ID_ZOOM_LEVEL_LAST)
            {
                m_canvas->SetZoom(ScaleMode::Fixed, Marconi::Zoom::kLevels[event.GetId() - ID_ZOOM_LEVEL_FIRST]);
            }
            else if (event.GetId() >= ID_QUALITY_FIRST && event.GetId() <= ID_QUALITY_LAST)
            {
                static const Marconi::PictureQuality kQualities[] = {Marconi::PictureQuality::Best,
                                                                      Marconi::PictureQuality::Balanced,
                                                                      Marconi::PictureQuality::LowBandwidth};
                m_quality = kQualities[event.GetId() - ID_QUALITY_FIRST];
                Marconi::QualitySettings settings = Marconi::SettingsFor(m_quality);
                if (m_session)
                {
                    m_session->SetQuality(settings.compressLevel, settings.qualityLevel);
                }
            }
            else if (event.GetId() >= ID_DISPLAY_FIRST && event.GetId() <= ID_DISPLAY_LAST)
            {
                size_t index = static_cast<size_t>(event.GetId() - ID_DISPLAY_FIRST);
                if (index < m_screens.size())
                {
                    m_displayId = static_cast<long>(m_screens[index].id);
                    ApplyDisplayChoice();
                }
            }
            break;
        }
        UpdateControls();
        UpdateStatus();
        m_canvas->SetFocus();
        SyncClipboardToServer();
    }

    void ViewerFrame::SendKeyCombination(std::initializer_list<uint32_t> keysyms)
    {
        if (m_canvas->IsViewOnly() || !m_session)
        {
            return;
        }
        m_canvas->ReleaseInput();
        for (uint32_t keysym : keysyms)
        {
            m_session->SendKeyEvent(keysym, true);
        }
        for (auto it = std::rbegin(keysyms); it != std::rend(keysyms); ++it)
        {
            m_session->SendKeyEvent(*it, false);
        }
    }

    void ViewerFrame::ToggleFullScreen()
    {
        m_canvas->ReleaseInput();
        if (!IsFullScreen())
        {
            // Docked panels would cover part of the screen; floating ones stay where the user put them.
            m_layoutBeforeFullScreen = m_aui.SavePerspective();
            wxAuiPaneInfoArray& panes = m_aui.GetAllPanes();
            for (size_t i = 0; i < panes.GetCount(); ++i)
            {
                if (panes[i].name != "Screen" && panes[i].name != "ConnectionLost" && !panes[i].IsFloating())
                {
                    panes[i].Hide();
                }
            }
            m_aui.Update();
            ShowFullScreen(true, wxFULLSCREEN_ALL);
            m_fullScreenBar->Begin();
            return;
        }
        m_fullScreenBar->End();
        ShowFullScreen(false, wxFULLSCREEN_ALL);
        m_aui.LoadPerspective(m_layoutBeforeFullScreen);
        FitToDesktop();
    }

    void ViewerFrame::UpdateControls()
    {
        m_toolBar->ToggleTool(ID_SCALE_FIT, m_canvas->GetScaleMode() == ScaleMode::Fit);
        m_toolBar->ToggleTool(ID_VIEW_ONLY, m_canvas->IsViewOnly());
        m_toolBar->ToggleTool(ID_CLIPBOARD, m_clipboard.IsEnabled());
        for (int id : {ID_SEND_CTRL_ALT_DEL, ID_SEND_KEYS, ID_SEND_F8, ID_SEND_F9, ID_SEND_F10})
        {
            m_toolBar->EnableTool(id, m_connected && !m_canvas->IsViewOnly());
        }
        m_toolBar->EnableTool(ID_REFRESH, m_connected);
        m_toolBar->EnableTool(ID_SCREENSHOT, m_connected && m_canvas->HasPicture());
        m_toolBar->ToggleTool(ID_FILES, m_files != nullptr && m_files->IsShown());
        m_toolBar->Refresh();
    }

    void ViewerFrame::ToggleFiles()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Files");
        if (pane.IsShown())
        {
            pane.Hide();
            m_aui.Update();
            m_canvas->SetFocus();
        }
        else
        {
            ShowFiles();
        }
        UpdateControls();
    }

    void ViewerFrame::ShowFiles()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Files");
        if (!pane.IsShown())
        {
            pane.Show();
            m_aui.Update();
        }
        m_files->Activate();
    }

    void ViewerFrame::ToggleToolbar()
    {
        wxAuiPaneInfo& pane = m_aui.GetPane("Toolbar");
        pane.Show(!pane.IsShown());
        m_aui.Update();
    }

    void ViewerFrame::UpdateStatus()
    {
        if (!m_connected)
        {
            return;
        }
        wxSize desktop = m_canvas->GetDesktopSize();
        wxString status = wxString::Format("Connected to %s (%d x %d)", GetServerDisplayName(), desktop.x, desktop.y);
        // Always say whether the connection is encrypted: without encryption, the picture and every key typed can
        // be read by anyone on the network between the two computers.
        bool encrypted = m_session && m_session->IsEncrypted();
        status += encrypted ? " - encrypted, " + wxString::FromUTF8(m_session->GetEncryptionName())
                            : wxString(" - not encrypted");
        if (m_waitingForPicture)
        {
            status += " - waiting for the screen...";
        }
        status += wxString::Format(" - %d%%", m_canvas->GetShownPercent());
        for (size_t i = 0; i < m_screens.size(); ++i)
        {
            if (!m_canvas->GetViewport().IsEmpty() && static_cast<long>(m_screens[i].id) == m_displayId)
            {
                status += wxString::Format(" - display %zu of %zu", i + 1, m_screens.size());
            }
        }
        if (m_canvas->IsViewOnly())
        {
            status += " - view only";
        }
        if (m_systemKeys && m_systemKeys->IsActive())
        {
            status += " - system shortcuts go to the other computer";
        }
        SetStatusText(status);
    }
} // namespace MarconiUI
