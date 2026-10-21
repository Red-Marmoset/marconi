// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Command ids shared by the viewer window's toolbar, menus and full-screen bar

#pragma once

#include <MarconiCore/Zoom.h>

#include <wx/defs.h>

namespace MarconiUI
{
    enum CommandId
    {
        ID_SCALE_FIT = wxID_HIGHEST + 100,
        ID_ZOOM_IN,
        ID_ZOOM_OUT,
        ID_ZOOM_LEVEL_FIRST, // one id per Marconi::Zoom::kLevels entry
        ID_ZOOM_LEVEL_LAST = ID_ZOOM_LEVEL_FIRST + static_cast<int>(Marconi::Zoom::kLevelCount) - 1,
        ID_FULL_SCREEN,
        ID_VIEW_ONLY,
        ID_CLIPBOARD,
        ID_SEND_CLIPBOARD,
        ID_GET_CLIPBOARD,
        ID_PASTE_FILES,
        ID_SEND_CTRL_ALT_DEL,
        ID_SEND_ALT_TAB,
        ID_SEND_ALT_F4,
        ID_SEND_WINDOWS_KEY,
        ID_SEND_CTRL_ESC,
        ID_SEND_CTRL_SHIFT_ESC,
        ID_SEND_PRINT_SCREEN,
        ID_SEND_CMD_TAB,
        ID_SEND_F8,
        ID_SEND_F9,
        ID_SEND_F10,
        ID_SEND_KEYS,   // the toolbar button that opens the Send Keys menu
        ID_SYSTEM_KEYS, // send system shortcuts to the other computer
        ID_SCREENSHOT,  // the toolbar button that opens the Screenshot menu
        ID_SCREENSHOT_SAVE,
        ID_SCREENSHOT_COPY,
        ID_SCREENSHOT_POINTER,
        ID_SHOW_TOOLBAR,
        ID_SHOW_MENU, // the full-screen bar's Menu button
        ID_DISPLAY_ALL,
        ID_DISPLAY_FIRST,
        ID_DISPLAY_LAST = ID_DISPLAY_FIRST + 15, // up to 16 remote displays
        ID_MATCH_WINDOW_SIZE,
        ID_QUALITY_FIRST,
        ID_QUALITY_LAST = ID_QUALITY_FIRST + 2, // Best, Balanced, Low bandwidth
        ID_FILES,
        ID_REFRESH,
        ID_SHOW_PERFORMANCE, // the timing readout over the picture
        ID_DISCONNECT,       // keep last: the viewer binds its commands from ID_SCALE_FIT to here
    };
} // namespace MarconiUI
