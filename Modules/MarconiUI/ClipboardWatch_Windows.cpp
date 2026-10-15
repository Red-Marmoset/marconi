// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clipboard change counter on Windows

#include "ClipboardWatch.h"

#include <windows.h>

namespace MarconiUI
{
    long GetClipboardChangeCount()
    {
        return static_cast<long>(GetClipboardSequenceNumber());
    }
} // namespace MarconiUI
