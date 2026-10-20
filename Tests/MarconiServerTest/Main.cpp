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
        MarconiServerTest::RunSessionTests();
        MarconiServerTest::RunListenerTests();
        MarconiServerTest::RunEndToEndTests();
        printf("\nMarconiServerTest: %d passed, %d failed\n", MarconiServerTest::g_passed, MarconiServerTest::g_failed);
        return MarconiServerTest::g_failed;
    }
} // namespace Test
