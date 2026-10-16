// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiCoreTest entry point

#include "TestSupport.h"

#include <Test/Test.h>

namespace Test
{
    int TestMain(const CommandLine::Parser&)
    {
        MarconiTest::RunAddressTests();
        MarconiTest::RunTextTests();
        MarconiTest::RunAuthTests();
        MarconiTest::RunStreamTests();
        MarconiTest::RunHandshakeTests();
        MarconiTest::RunClientTests();
        MarconiTest::RunDecoderTests();
        MarconiTest::RunVersionTests();
        MarconiTest::RunSocketTests();
        MarconiTest::RunProbeTests();
        MarconiTest::RunChoiceTests();
        MarconiTest::RunDiscoveryTests();
        MarconiTest::RunVeNCryptTests();
        MarconiTest::RunRandomInputTests();
        MarconiTest::RunLocalKeysTests();
        MarconiTest::RunFilesTests();
        MarconiTest::RunTightFilesTests();
        MarconiTest::RunFileViewTests();

        printf("\nMarconiCoreTest: %d passed, %d failed\n", MarconiTest::g_passed, MarconiTest::g_failed);
        return MarconiTest::g_failed;
    }
} // namespace Test
