// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Whether the user is working in a given window right now: compares with Windows' foreground window

#include "ForegroundWindow.h"

#include <wx/msw/private.h>

namespace MarconiUI
{
    bool IsForegroundWindow(wxTopLevelWindow& window)
    {
        // Not IsActive(): on Windows it only asks whether the window holds Marconi's own keyboard focus, which a
        // window in the background also does once it calls SetFocus (after reconnecting, for example).
        return ::GetForegroundWindow() == static_cast<HWND>(window.GetHWND());
    }
} // namespace MarconiUI
