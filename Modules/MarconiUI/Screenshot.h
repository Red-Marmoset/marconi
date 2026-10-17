// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Screenshots of the remote desktop: composing the picture and naming the file

#pragma once

#include <MarconiCore/Rfb/Client.h>

#include <wx/datetime.h>
#include <wx/gdicmn.h>
#include <wx/image.h>
#include <wx/string.h>

namespace MarconiUI
{
    // The remote desktop's picture with, if `cursor` is given, the remote mouse pointer drawn at `pointer` (desktop
    // coordinates of its hotspot), blended by its alpha. A pointer off the picture is left out.
    wxImage ComposeScreenshot(const wxImage& picture, const Marconi::Rfb::CursorShape* cursor, const wxPoint& pointer);

    // "<computer> 2026-10-16 14-05-09.png": the computer's name with characters file names can't take replaced.
    wxString SuggestScreenshotName(const wxString& computerName, const wxDateTime& when);
} // namespace MarconiUI
