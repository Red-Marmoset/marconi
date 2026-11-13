// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: helpers shared by the Wayland tests (WaylandTests.cpp, WaylandBusTests.cpp)

#pragma once

#include "TestSupport.h"

#if defined(__linux__)

#include "WaylandFakes.h"

#include <MarconiServerCore/WaylandPortal.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace MarconiServerTest
{
    namespace WaylandTestSupport
    {
        using namespace Marconi;
        using namespace Marconi::Server;

        constexpr uint32_t kRed = 0xFF0000;
        constexpr uint32_t kGreen = 0x00FF00;
        constexpr uint32_t kBlue = 0x0000FF;

        inline std::string TempFile(const char* name)
        {
            static int counter = 0;
            std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                        ("marconi-wayland-test-" + std::to_string(getpid()) + "-" + std::to_string(++counter));
            std::filesystem::create_directories(dir);
            return (dir / name).string();
        }

        inline WaylandPortalOptions OptionsFor(const PrivateBus& bus, const std::string& tokenPath)
        {
            WaylandPortalOptions options;
            options.busAddress = bus.Address();
            options.tokenPath = tokenPath;
            options.approvalTimeoutMs = 10000;
            return options;
        }

        inline bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

        inline bool Has(const std::vector<std::string>& lines, const std::string& line)
        {
            for (const auto& entry : lines)
            {
                if (entry == line)
                {
                    return true;
                }
            }
            return false;
        }

        // Waits (up to `ms`) for `condition` to hold.
        template <typename F>
        inline bool Eventually(F condition, int ms = 5000)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (condition())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return condition();
        }

        // Runs a test and says so when it was slow, since a hang in a portal or PipeWire call shows up as time.
        inline void TimedTest(const char* name, const std::function<void()>& body)
        {
            auto start = std::chrono::steady_clock::now();
            RunTest(name, body);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (ms > 3000)
            {
                printf("  (slow: %lld ms)\n", static_cast<long long>(ms));
            }
        }

    } // namespace WaylandTestSupport

    void RunWaylandPureTests();
    void RunWaylandBusTests(PrivateBus& bus);
} // namespace MarconiServerTest

#endif
