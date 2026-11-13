// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No secure desktop outside Windows: following the input desktop does nothing, and there is no Ctrl+Alt+Del to send

#include "DesktopSwitch.h"

namespace Marconi
{
    namespace Server
    {
        bool FollowInputDesktop()
        {
            return false;
        }

        std::string GetInputDesktopName()
        {
            return std::string();
        }

        bool SendSecureAttention()
        {
            return false;
        }
    } // namespace Server
} // namespace Marconi
