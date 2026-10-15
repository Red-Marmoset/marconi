// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RmVncCoreTest entry point

#include "TestSupport.h"

#include <Test/Test.h>

namespace Test
{
    int TestMain(const CommandLine::Parser&)
    {
        RmVncTest::RunAddressTests();
        RmVncTest::RunTextTests();
        RmVncTest::RunAuthTests();
        RmVncTest::RunStreamTests();
        RmVncTest::RunHandshakeTests();
        RmVncTest::RunClientTests();
        RmVncTest::RunDecoderTests();

        printf("\nRmVncCoreTest: %d passed, %d failed\n", RmVncTest::g_passed, RmVncTest::g_failed);
        return RmVncTest::g_failed;
    }
} // namespace Test
