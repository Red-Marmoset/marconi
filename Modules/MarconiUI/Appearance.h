// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Persistent appearance preferences and the shared desktop UI palette.
#pragma once

#include <wx/colour.h>
#include <wx/string.h>

class wxWindow;

namespace MarconiUI
{
    enum class ThemeMode { System, Light, Dark };

    struct ThemePalette
    {
        wxColour background;
        wxColour surface;
        wxColour text;
        wxColour muted;
        wxColour border;
        wxColour accent;
        wxColour selection;
    };

    ThemeMode GetThemeMode();
    void InitializeAppearance();
    bool SaveThemeMode(ThemeMode mode);
    wxString GetAppearanceNotice();
    ThemePalette GetThemePalette();
    void StyleWindow(wxWindow* window);
} // namespace MarconiUI
