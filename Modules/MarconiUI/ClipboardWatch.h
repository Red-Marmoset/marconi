// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Cheap check for local clipboard changes, so the viewer can send them without reading the clipboard constantly

#pragma once

namespace MarconiUI
{
    // A number that changes whenever the system clipboard changes, or -1 where that isn't available (the viewer
    // then only sends the clipboard when its window is activated).
    long GetClipboardChangeCount();
} // namespace MarconiUI
