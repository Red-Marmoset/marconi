// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The keys the viewer keeps for itself instead of sending them to the remote computer

#include <MarconiCore/LocalKeys.h>

namespace Marconi
{
    LocalCommand GetLocalCommand(int functionKey, bool modifiers)
    {
        if (modifiers)
        {
            return LocalCommand::None;
        }
        switch (functionKey)
        {
            case 8: return LocalCommand::Menu;
            case 9: return LocalCommand::Toolbar;
            case 10: return LocalCommand::Files;
            default: return LocalCommand::None;
        }
    }

    LocalCommand GetLocalZoomCommand(char character, bool ctrlOrCmd, bool alt)
    {
        if (!ctrlOrCmd || alt)
        {
            return LocalCommand::None;
        }
        switch (character)
        {
            case '+':
            case '=': return LocalCommand::ZoomIn;
            case '-': return LocalCommand::ZoomOut;
            case '0': return LocalCommand::ZoomReset;
            default: return LocalCommand::None;
        }
    }
} // namespace Marconi
