// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for LocalKeys: which function keys the viewer keeps instead of sending them to the remote computer

#include "TestSupport.h"

#include <MarconiCore/LocalKeys.h>

namespace MarconiTest
{
    using namespace Marconi;

    void RunLocalKeysTests()
    {
        RunTest("Marconi keeps F8, F9 and F10, only without modifiers", []() {
            CHECK(GetLocalCommand(8, false) == LocalCommand::Menu);
            CHECK(GetLocalCommand(9, false) == LocalCommand::Toolbar);
            CHECK(GetLocalCommand(8, true) == LocalCommand::None);
            CHECK(GetLocalCommand(9, true) == LocalCommand::None);
            CHECK(GetLocalCommand(10, false) == LocalCommand::Files);
            CHECK(GetLocalCommand(10, true) == LocalCommand::None);
            CHECK(GetLocalCommand(1, false) == LocalCommand::None);
            CHECK(GetLocalCommand(0, false) == LocalCommand::None);
        });
    }
} // namespace MarconiTest
