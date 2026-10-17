// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the reconnect schedule

#include "TestSupport.h"

#include <MarconiCore/Reconnect.h>

namespace MarconiTest
{
    using namespace Marconi;

    void RunReconnectTests()
    {
        RunTest("Reconnect waits grow to 30 seconds and attempts stop after 5 minutes", []() {
            CHECK(Reconnect::DelaySeconds(1) == 2);
            CHECK(Reconnect::DelaySeconds(2) == 4);
            CHECK(Reconnect::DelaySeconds(3) == 8);
            CHECK(Reconnect::DelaySeconds(4) == 16);
            CHECK(Reconnect::DelaySeconds(5) == 30);
            CHECK(Reconnect::DelaySeconds(50) == 30);
            CHECK(Reconnect::DelaySeconds(0) == 2);
            CHECK(!Reconnect::ShouldGiveUp(0));
            CHECK(!Reconnect::ShouldGiveUp(299));
            CHECK(Reconnect::ShouldGiveUp(300));
            CHECK(Reconnect::ShouldGiveUp(1000));
        });
    }
} // namespace MarconiTest
