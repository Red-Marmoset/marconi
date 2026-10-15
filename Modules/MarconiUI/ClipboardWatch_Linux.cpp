// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clipboard change counter on Linux: not available without reading the clipboard

#include "ClipboardWatch.h"

namespace MarconiUI
{
    long GetClipboardChangeCount()
    {
        return -1;
    }
} // namespace MarconiUI
