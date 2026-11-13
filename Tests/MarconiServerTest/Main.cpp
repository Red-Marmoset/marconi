// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiServerTest entry point

#include "TestSupport.h"

#include <Test/Test.h>

#include <cstdio>

namespace Test
{
    int TestMain(const CommandLine::Parser&)
    {
        MarconiServerTest::RunPasswordTests();
        MarconiServerTest::RunIdentityTests();
        MarconiServerTest::RunSessionTests();
        MarconiServerTest::RunServiceTests();
        MarconiServerTest::RunRuleTests();
        MarconiServerTest::RunTrayTests();
        MarconiServerTest::RunListenerTests();
        MarconiServerTest::RunEndToEndTests();
        MarconiServerTest::RunX11Tests();
        MarconiServerTest::RunFileTests();
        MarconiServerTest::RunClipboardTests();
        MarconiServerTest::RunDisplayTests();
        MarconiServerTest::RunLinuxScreenTests();
        MarconiServerTest::RunLogTests();
        MarconiServerTest::RunGamepadTests();
        printf("\nMarconiServerTest: %d passed, %d failed\n", MarconiServerTest::g_passed, MarconiServerTest::g_failed);
        return MarconiServerTest::g_failed;
    }
} // namespace Test
