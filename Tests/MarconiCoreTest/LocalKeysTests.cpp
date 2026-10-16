// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for LocalKeys: which function keys the viewer keeps instead of sending them to the remote computer

#include "TestSupport.h"

#include <MarconiCore/LocalKeys.h>
#include <MarconiCore/Zoom.h>

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

        RunTest("Ctrl with plus, minus and 0 zoom locally; anything else goes to the remote computer", []() {
            CHECK(GetLocalZoomCommand('+', true, false) == LocalCommand::ZoomIn);
            CHECK(GetLocalZoomCommand('=', true, false) == LocalCommand::ZoomIn);
            CHECK(GetLocalZoomCommand('-', true, false) == LocalCommand::ZoomOut);
            CHECK(GetLocalZoomCommand('0', true, false) == LocalCommand::ZoomReset);
            CHECK(GetLocalZoomCommand('+', false, false) == LocalCommand::None);
            CHECK(GetLocalZoomCommand('+', true, true) == LocalCommand::None);
            CHECK(GetLocalZoomCommand('a', true, false) == LocalCommand::None);
            CHECK(GetLocalZoomCommand('1', true, false) == LocalCommand::None);
        });

        RunTest("Zoom steps through the fixed levels and stops at the ends", []() {
            CHECK(Zoom::Next(100, true) == 125);
            CHECK(Zoom::Next(100, false) == 75);
            CHECK(Zoom::Next(110, true) == 125);  // from a Fit percentage between levels
            CHECK(Zoom::Next(110, false) == 100);
            CHECK(Zoom::Next(200, true) == 200);
            CHECK(Zoom::Next(50, false) == 50);
            CHECK(Zoom::Next(10, true) == 50);
            CHECK(Zoom::Next(900, false) == 200);
            CHECK(Zoom::IsLevel(150) && !Zoom::IsLevel(110));
        });
    }
} // namespace MarconiTest
