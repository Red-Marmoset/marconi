// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Whether the user is working in a given window right now

#pragma once

#include <wx/toplevel.h>

namespace MarconiUI
{
    // True while the window is the one the user is working in: the active window of the program in front.
    bool IsForegroundWindow(wxTopLevelWindow& window);
} // namespace MarconiUI
