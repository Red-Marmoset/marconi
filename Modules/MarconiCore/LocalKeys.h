// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The keys the viewer keeps for itself instead of sending them to the remote computer

#pragma once

namespace Marconi
{
    enum class LocalCommand
    {
        None,      // send the key to the remote computer
        Menu,      // F8: the viewer's menu
        Toolbar,   // F9: show or hide the toolbar
        Files,     // F10: show or hide the file panel
        ZoomIn,    // Ctrl (Cmd on a Mac) and '+'
        ZoomOut,   // Ctrl and '-'
        ZoomReset, // Ctrl and '0': actual size
    };

    // What a function key pressed in the viewer does locally. `functionKey` is 1-24 (F1 = 1); `modifiers` is whether
    // Shift, Ctrl, Alt or Cmd is held, in which case the key always goes to the remote computer.
    LocalCommand GetLocalCommand(int functionKey, bool modifiers);

    // What a character key does locally: the zoom shortcuts, with Ctrl (Cmd on a Mac) and no Alt. `character` is
    // the key as typed ('=' counts as '+', since that's the same key on most keyboards); Shift doesn't matter.
    LocalCommand GetLocalZoomCommand(char character, bool ctrlOrCmd, bool alt);
} // namespace Marconi
