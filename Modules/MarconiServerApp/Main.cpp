// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Marconi server program. For now it serves a moving test pattern over the Marconi protocol, so the client
// can connect to a real server; capturing the actual screen (and injecting input) comes with the platform work.

#include <MarconiCore/Version.h>
#include <MarconiServerCore/ClipboardAccess.h>
#include <MarconiServerCore/IdentityStore.h>
#include <MarconiServerCore/LoggingInputSink.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/SystemInput.h>
#include <MarconiServerCore/SystemScreen.h>
#include <MarconiServerCore/TestPatternScreen.h>

#include "ServerModes.h"

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
        printf("  --allow-no-password   let viewers in with no password set (for testing only)\n");
        printf("  --password-hash HASH  require the password behind this hash (from --make-hash)\n");
        printf("  --make-hash TEXT      print the hash of a password and exit (store it, not the password)\n");
        printf("  --cert-file PATH      load the server's certificate from PATH (made on first run with --key-file)\n");
        printf("  --key-file PATH       load the server's private key from PATH (made on first run with --cert-file)\n");
        printf("  --no-files            don't let viewers browse or copy this computer's files\n");
        printf("  --file-root PATH      let viewers reach only this folder (default: the whole computer)\n");
        printf("  --lock-on-disconnect  lock this computer's screen when a viewer disconnects\n");
        printf("  --service             run as the Windows service (started by Windows, not by hand)\n");
        printf("  --session ID          the helper the service starts for console session ID\n");
        printf("  --data-dir DIR        the settings and certificate folder for --session and --set-password\n");
        printf("  --set-password        read a password from standard input and store its hash in the settings\n");
        printf("  --install-service     (administrator) install the Windows service and start it [--no-start]\n");
        printf("  --remove-service      (administrator) stop and remove the Windows service\n");
        printf("  --control-status      ask the running helper who is connected (Windows service)\n");
        printf("  --control-disconnect ID  end viewer ID's connection (0 = everyone)\n");
        printf("  --control-set-password   change the password of the running helper (password on standard input)\n");
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
    std::string certFile;
    std::string keyFile;
    enum class Mode
    {
        Console,
        Service,
        Session,
        SetPassword,
        InstallService,
        RemoveService,
        Control
    } mode = Mode::Console;
    std::string dataDirectory;
    unsigned consoleSession = 0;
    std::string controlCommand;
    unsigned controlViewerId = 0;
    int portOverride = -1;
    bool startServiceNow = true;

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
        else if (arg == "--lock-on-disconnect")
        {
            settings.lockOnDisconnect = true;
        }
        else if (arg == "--service")
        {
            mode = Mode::Service;
        }
        else if (arg == "--session")
        {
            std::string value;
            if (!TakeValue(argc, argv, i, "--session", value))
            {
                return 2;
            }
            mode = Mode::Session;
            consoleSession = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (arg == "--data-dir")
        {
            if (!TakeValue(argc, argv, i, "--data-dir", dataDirectory))
            {
                return 2;
            }
        }
        else if (arg == "--set-password")
        {
            mode = Mode::SetPassword;
        }
        else if (arg == "--install-service")
        {
            mode = Mode::InstallService;
        }
        else if (arg == "--control-status")
        {
            mode = Mode::Control;
            controlCommand = "status";
        }
        else if (arg == "--control-disconnect")
        {
            std::string value;
            if (!TakeValue(argc, argv, i, "--control-disconnect", value))
            {
                return 2;
            }
            mode = Mode::Control;
            controlCommand = "disconnect";
            controlViewerId = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (arg == "--control-set-password")
        {
            mode = Mode::Control;
            controlCommand = "set-password";
        }
        else if (arg == "--no-start")
        {
            startServiceNow = false;
        }
        else if (arg == "--remove-service")
        {
            mode = Mode::RemoveService;
        }
        else if (arg == "--port")
        {
            std::string value;
            if (!TakeValue(argc, argv, i, "--port", value))
            {
                return 2;
            }
            settings.port = static_cast<uint16_t>(std::atoi(value.c_str()));
            portOverride = std::atoi(value.c_str());
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
        else if (arg == "--no-files")
        {
            settings.filesEnabled = false;
        }
        else if (arg == "--file-root")
        {
            if (!TakeValue(argc, argv, i, "--file-root", settings.fileRoot))
            {
                return 2;
            }
        }
        else if (arg == "--allow-no-password")
        {
            settings.allowNoPassword = true;
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

    switch (mode)
    {
    case Mode::Service:
        return Marconi::Server::RunServiceMode();
    case Mode::Session:
        return Marconi::Server::RunSessionMode(dataDirectory, consoleSession, forceTestPattern, portOverride);
    case Mode::SetPassword:
        return Marconi::Server::RunSetPasswordMode(dataDirectory);
    case Mode::InstallService:
        return Marconi::Server::RunInstallServiceMode(startServiceNow);
    case Mode::RemoveService:
        return Marconi::Server::RunRemoveServiceMode();
    case Mode::Control:
        return Marconi::Server::RunControlMode(controlCommand, controlViewerId);
    case Mode::Console:
        break;
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
            printf("Can't share this computer's screen.\n%s\nServing a test pattern instead.\n", screenError.c_str());
        }
        else
        {
            std::string inputError;
            systemInput = Marconi::Server::CreateSystemInput(inputError);
            if (!systemInput)
            {
                printf("Viewers will see this screen but can't control it.\n%s\n",
                       inputError.empty() ? "Mouse and keyboard control isn't available here yet." : inputError.c_str());
            }
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

    // Share the clipboard only with the real screen: the test pattern stands in for a computer that isn't there.
    std::unique_ptr<Marconi::Server::ClipboardAccess> systemClipboard;
    if (systemScreen)
    {
        systemClipboard = Marconi::Server::CreateSystemClipboard();
    }

    Marconi::Server::Listener listener(*screen, *input, settings);
    listener.SetClipboard(systemClipboard.get());
    std::string error;
    if (!listener.Start(error))
    {
        printf("Could not start the server: %s\n", error.c_str());
        return 1;
    }
    printf("Marconi Server %s serving %s on port %u. Connect with marconi://this-computer\n",
           Marconi::GetVersionString(), what, static_cast<unsigned>(listener.GetPort()));
    printf("Clipboard sharing: %s\n", systemClipboard ? "on" : "off");
    printf("Certificate fingerprint: %s\n", listener.GetCertificateFingerprint().c_str());
    if (settings.passwordHash.empty())
    {
        if (settings.allowNoPassword)
        {
            printf("No password set, and --allow-no-password is on: any viewer that reaches this port can connect.\n");
        }
        else
        {
            printf("No password set: every login is refused until one is set. Use --password (or --allow-no-password for testing).\n");
        }
    }
    else
    {
        printf("A password is required to connect.\n");
    }
    printf("Press Ctrl+C to stop.\n");
    listener.Run(); // until the process is stopped
    return 0;
}
