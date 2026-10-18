// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi server program. For now it serves a moving test pattern over the Marconi protocol, so the client
// can connect to a real server; capturing the actual screen (and injecting input) comes with the platform work.

#include "LoggingInputSink.h"

#include <MarconiCore/Version.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/SystemInput.h>
#include <MarconiServerCore/SystemScreen.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include <memory>

#include <Log/Log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    void PrintUsage()
    {
        printf("Marconi Server %s\n\n", Marconi::GetVersionString());
        printf("Usage: MarconiServer [options]\n");
        printf("  --test-pattern        serve a moving test pattern (the only mode until screen capture lands)\n");
        printf("  --port N              listen on port N (default %u)\n",
               static_cast<unsigned>(Marconi::Wire::kDefaultPort));
        printf("  --size WxH            the test pattern's size (default 1280x720)\n");
        printf("  --name NAME           the name shown to viewers (default: this computer)\n");
        printf("  --help                show this\n");
    }

    // Reads the value after a flag, or prints an error and returns false.
    bool TakeValue(int argc, char** argv, int& i, const char* flag, std::string& out)
    {
        if (i + 1 >= argc)
        {
            printf("%s needs a value\n", flag);
            return false;
        }
        out = argv[++i];
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    Marconi::Server::Settings settings;
    settings.serverVersion = Marconi::GetVersionString();
    int width = 1280;
    int height = 720;
    bool forceTestPattern = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            return 0;
        }
        else if (arg == "--test-pattern")
        {
            forceTestPattern = true;
        }
        else if (arg == "--port")
        {
            std::string value;
            if (!TakeValue(argc, argv, i, "--port", value))
            {
                return 2;
            }
            settings.port = static_cast<uint16_t>(std::atoi(value.c_str()));
        }
        else if (arg == "--name")
        {
            if (!TakeValue(argc, argv, i, "--name", settings.serverName))
            {
                return 2;
            }
        }
        else if (arg == "--size")
        {
            std::string value;
            if (!TakeValue(argc, argv, i, "--size", value))
            {
                return 2;
            }
            if (std::sscanf(value.c_str(), "%dx%d", &width, &height) != 2 || width < 1 || height < 1)
            {
                printf("--size must look like 1280x720\n");
                return 2;
            }
        }
        else
        {
            printf("Unknown option: %s\n", arg.c_str());
            PrintUsage();
            return 2;
        }
    }

    // The real screen and input where this platform has them (Windows for now), unless --test-pattern was asked.
    // Elsewhere, and when there is no capture yet, serve the moving test pattern and log the input.
    std::unique_ptr<Marconi::Server::ScreenSource> systemScreen;
    std::unique_ptr<Marconi::Server::InputSink> systemInput;
    std::unique_ptr<Marconi::Server::TestPatternScreen> pattern;
    Marconi::Server::LoggingInputSink logging;
    const char* what = nullptr;

    if (!forceTestPattern)
    {
        std::string screenError;
        systemScreen = Marconi::Server::CreateSystemScreen(screenError);
        if (!systemScreen)
        {
            printf("No screen capture on this computer yet (%s); serving the test pattern instead.\n",
                   screenError.c_str());
        }
        else
        {
            systemInput = Marconi::Server::CreateSystemInput();
        }
    }

    Marconi::Server::ScreenSource* screen = systemScreen.get();
    Marconi::Server::InputSink* input = systemInput ? systemInput.get() : static_cast<Marconi::Server::InputSink*>(&logging);
    if (screen == nullptr)
    {
        pattern = std::make_unique<Marconi::Server::TestPatternScreen>(width, height);
        screen = pattern.get();
        what = "a test pattern";
    }
    else
    {
        what = "this computer's screen";
    }

    Marconi::Server::Listener listener(*screen, *input, settings);
    std::string error;
    if (!listener.Start(error))
    {
        printf("Could not start the server: %s\n", error.c_str());
        return 1;
    }
    printf("Marconi Server %s serving %s on port %u. Connect with marconi://this-computer\n",
           Marconi::GetVersionString(), what, static_cast<unsigned>(listener.GetPort()));
    printf("Press Ctrl+C to stop.\n");
    listener.Run(); // until the process is stopped
    return 0;
}
