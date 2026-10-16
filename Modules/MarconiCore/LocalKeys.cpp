// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The function keys the viewer keeps for itself instead of sending them to the remote computer

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
            default: return LocalCommand::None;
        }
    }
} // namespace Marconi
