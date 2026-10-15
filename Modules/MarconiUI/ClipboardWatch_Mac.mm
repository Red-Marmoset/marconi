// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Clipboard change counter on macOS

#include "ClipboardWatch.h"

#import <AppKit/AppKit.h>

namespace MarconiUI
{
    long GetClipboardChangeCount()
    {
        return static_cast<long>([[NSPasteboard generalPasteboard] changeCount]);
    }
} // namespace MarconiUI
