// Copyright (c) 2026 Red Marmoset Studios Ltd.
// System shortcut capture on Linux: not available (Wayland gives applications no way to take the desktop's
// shortcuts, and most desktops now run on it)

#include "SystemKeys.h"

namespace MarconiUI
{
    bool SystemKeyCapture::IsSupported()
    {
        return false;
    }

    std::string SystemKeyCapture::GetUnsupportedReason()
    {
        return "Linux desktops don't let a program take over shortcuts such as Alt+Tab";
    }

    std::unique_ptr<SystemKeyCapture> SystemKeyCapture::Create(SendKey)
    {
        return nullptr;
    }
} // namespace MarconiUI
