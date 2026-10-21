// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi server program. For now it serves a moving test pattern over the Marconi protocol, so the client
// can connect to a real server; capturing the actual screen (and injecting input) comes with the platform work.

#include <MarconiCore/Version.h>
#include <MarconiServerCore/IdentityStore.h>
#include <MarconiServerCore/Bench.h>
#include <MarconiServerCore/LoggingInputSink.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/PasswordHash.h>
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
        printf("  --password TEXT       require this access password (for testing; it shows in process lists)\n");
        printf("  --password-hash HASH  require the password behind this hash (from --make-hash)\n");
        printf("  --make-hash TEXT      print the hash of a password and exit (store it, not the password)\n");
        printf("  --cert-file PATH      load the server's certificate from PATH (made on first run with --key-file)\n");
        printf("  --key-file PATH       load the server's private key from PATH (made on first run with --cert-file)\n");
        printf("  --bench [SECONDS]     read and compress the screen for SECONDS (default 10) with no viewer, then\n");
        printf("                        print how long each stage took\n");
        printf("  --help                show this\n");
        printf("\nWithout --cert-file/--key-file the server makes a fresh certificate each run, so viewers are\n");
        printf("asked to trust it again every time. Point both at files to keep one identity across restarts.\n");
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
    int benchSeconds = 0; // --bench: run the benchmark for this long instead of serving
    std::string certFile;
    std::string keyFile;

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
        else if (arg == "--bench")
        {
            benchSeconds = 10;
            // An optional number of seconds follows.
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
            {
                benchSeconds = std::atoi(argv[++i]);
                if (benchSeconds < 1)
                {
                    printf("--bench needs at least 1 second\n");
                    return 2;
                }
            }
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
        else if (arg == "--password")
        {
            std::string text;
            if (!TakeValue(argc, argv, i, "--password", text))
            {
                return 2;
            }
            settings.passwordHash = Marconi::Server::HashPassword(text);
        }
        else if (arg == "--password-hash")
        {
            if (!TakeValue(argc, argv, i, "--password-hash", settings.passwordHash))
            {
                return 2;
            }
        }
        else if (arg == "--make-hash")
        {
            std::string text;
            if (!TakeValue(argc, argv, i, "--make-hash", text))
            {
                return 2;
            }
            std::string hash = Marconi::Server::HashPassword(text);
            if (hash.empty())
            {
                printf("Could not get secure random numbers to make the hash.\n");
                return 1;
            }
            printf("%s\n", hash.c_str());
            return 0;
        }
        else if (arg == "--cert-file")
        {
            if (!TakeValue(argc, argv, i, "--cert-file", certFile))
            {
                return 2;
            }
        }
        else if (arg == "--key-file")
        {
            if (!TakeValue(argc, argv, i, "--key-file", keyFile))
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
    std::unique_ptr<Marconi::Server::PatternEchoInput> patternInput;
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
        // The pattern draws where the viewer's pointer is, so input latency shows in the picture.
        patternInput = std::make_unique<Marconi::Server::PatternEchoInput>(*input, *pattern);
        input = patternInput.get();
        what = "a test pattern";
    }
    else
    {
        what = "this computer's screen";
    }

    if (benchSeconds > 0)
    {
        Marconi::Server::BenchOptions bench;
        bench.durationMs = benchSeconds * 1000;
        printf("Reading and compressing %s for %d seconds...\n", what, benchSeconds);
        Marconi::Server::BenchResult result = Marconi::Server::RunBench(*screen, bench);
        printf("%s", Marconi::Server::FormatBench(result, what).c_str());
        return 0;
    }

    // A kept identity needs both files named; a fresh-each-run identity needs neither. One without the other is a
    // mistake worth catching before the server starts.
    if (certFile.empty() != keyFile.empty())
    {
        printf("Give both --cert-file and --key-file, or neither.\n");
        return 2;
    }
    if (!certFile.empty())
    {
        std::string identityError;
        if (!Marconi::Server::LoadOrCreateIdentity(certFile, keyFile, settings.serverName, settings, identityError))
        {
            printf("%s\n", identityError.c_str());
            return 1;
        }
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
    printf("Certificate fingerprint: %s\n", listener.GetCertificateFingerprint().c_str());
    if (settings.passwordHash.empty())
    {
        printf("No password set: any viewer that reaches this port can connect. Use --password to require one.\n");
    }
    else
    {
        printf("A password is required to connect.\n");
    }
    printf("Press Ctrl+C to stop.\n");
    listener.Run(); // until the process is stopped
    return 0;
}
