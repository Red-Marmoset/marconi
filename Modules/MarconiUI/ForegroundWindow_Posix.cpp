// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Whether the user is working in a given window right now: the key window on macOS, the focused window on GTK

#include "ForegroundWindow.h"

namespace MarconiUI
{
    bool IsForegroundWindow(wxTopLevelWindow& window)
    {
        return window.IsActive();
    }
} // namespace MarconiUI
