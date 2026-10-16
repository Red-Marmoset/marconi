// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keep application surfaces responsive while respecting native wx appearance limitations.
#include "Appearance.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/config.h>
#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/window.h>

namespace MarconiUI
{
    namespace
    {
        ThemeMode s_nativeMode = ThemeMode::System;
        wxString s_notice;

        wxApp::Appearance NativeAppearance(ThemeMode mode)
        {
            return mode == ThemeMode::Dark ? wxApp::Appearance::Dark :
                   mode == ThemeMode::Light ? wxApp::Appearance::Light : wxApp::Appearance::System;
        }
    }

    ThemeMode GetThemeMode()
    {
        const wxString mode = wxConfigBase::Get()->Read("/Appearance/Theme", "system");
        return mode == "dark" ? ThemeMode::Dark : mode == "light" ? ThemeMode::Light : ThemeMode::System;
    }

    void InitializeAppearance()
    {
        s_nativeMode = GetThemeMode();
        if (wxTheApp->SetAppearance(NativeAppearance(s_nativeMode)) == wxApp::AppearanceResult::Failure)
        {
            s_notice = "Native appearance is controlled by this desktop environment.";
        }
    }

    bool SaveThemeMode(ThemeMode mode)
    {
        wxConfigBase* config = wxConfigBase::Get();
        const wxString previous = config->Read("/Appearance/Theme", "system");
        const wxString value = mode == ThemeMode::Dark ? "dark" : mode == ThemeMode::Light ? "light" : "system";
        if (!config->Write("/Appearance/Theme", value) || !config->Flush())
        {
            config->Write("/Appearance/Theme", previous);
            return false;
        }
        if (mode == s_nativeMode)
        {
            s_notice.clear();
            return true;
        }
        switch (wxTheApp->SetAppearance(NativeAppearance(mode)))
        {
        case wxApp::AppearanceResult::Ok:
            s_nativeMode = mode;
            s_notice.clear();
            break;
        case wxApp::AppearanceResult::CannotChange:
            s_notice = "Saved. Restart Marconi to update native menus and title bars.";
            break;
        case wxApp::AppearanceResult::Failure:
            s_notice = "Saved. Native appearance is controlled by this desktop environment.";
            break;
        }
        return true;
    }

    wxString GetAppearanceNotice()
    {
        return s_notice;
    }

    ThemePalette GetThemePalette()
    {
        const ThemeMode mode = GetThemeMode();
        const bool dark = mode == ThemeMode::Dark ||
            (mode == ThemeMode::System && wxSystemSettings::GetAppearance().AreAppsDark());
        if (dark)
        {
            return {wxColour(34, 38, 43), wxColour(42, 47, 54), wxColour(242, 244, 247),
                    wxColour(178, 188, 201), wxColour(69, 77, 88), wxColour(194, 35, 55), wxColour(61, 78, 99)};
        }
        return {wxColour(250, 251, 253), wxColour(255, 255, 255), wxColour(27, 35, 47),
                wxColour(87, 101, 120), wxColour(215, 221, 230), wxColour(178, 34, 52), wxColour(226, 237, 250)};
    }

    void StyleWindow(wxWindow* window)
    {
        const ThemePalette palette = GetThemePalette();
        window->SetBackgroundColour(palette.background);
        window->SetForegroundColour(palette.text);
        if (wxDynamicCast(window, wxTextCtrl) || wxDynamicCast(window, wxListCtrl) ||
            wxDynamicCast(window, wxButton))
        {
            window->SetBackgroundColour(palette.surface);
        }
        if (wxDynamicCast(window, wxStaticLine))
        {
            window->SetForegroundColour(palette.border);
            window->SetBackgroundColour(palette.border);
        }
        for (wxWindow* child : window->GetChildren())
        {
            StyleWindow(child);
        }
        window->Refresh();
    }
} // namespace MarconiUI
