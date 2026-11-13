// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The shortcuts in the Send Keys menu, grouped by the other computer's system and then by what they do

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MarconiUI
{
    enum class RemoteSystem
    {
        Windows,
        Mac,
        Linux,
    };

    struct SendKeysShortcut
    {
        RemoteSystem system;
        const char* group; // the submenu under the system's menu, or nullptr for an item directly in it
        const char* label;
        std::vector<uint32_t> keysyms; // pressed in order, released in reverse
    };

    // In menu order: a system's items are together, and so are a group's.
    const std::vector<SendKeysShortcut>& GetSendKeysShortcuts();

    const char* GetRemoteSystemName(RemoteSystem system);

    // Menu ids for the shortcuts start at ID_SEND_SHORTCUT_FIRST and leave room for this many.
    constexpr size_t kMaxSendKeysShortcuts = 64;
} // namespace MarconiUI
