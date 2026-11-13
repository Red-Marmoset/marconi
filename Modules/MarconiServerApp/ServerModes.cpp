// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The command-line modes the service setup needs; see ServerModes.h

#include "ServerModes.h"

#include <MarconiServerCore/ControlPipe.h>
#include <MarconiServerCore/ControlSession.h>
#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/ServiceConfig.h>
#include <MarconiServerCore/ServiceHost.h>
#include <MarconiServerCore/SessionHost.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // The password for --set-password: the MARCONI_SET_PASSWORD environment variable when it is set (an
            // installer can pass it to the program without it touching a file or a command line), else one line of
            // standard input. False if neither gave anything.
            bool ReadPassword(std::string& password)
            {
                std::string fromEnvironment;
#ifdef _WIN32
                char* value = nullptr;
                size_t length = 0;
                if (_dupenv_s(&value, &length, "MARCONI_SET_PASSWORD") == 0 && value != nullptr)
                {
                    fromEnvironment = value;
                    free(value);
                }
#else
                if (const char* value = std::getenv("MARCONI_SET_PASSWORD"))
                {
                    fromEnvironment = value;
                }
#endif
                if (!fromEnvironment.empty())
                {
                    password = fromEnvironment;
                    return true;
                }
                if (!std::getline(std::cin, password))
                {
                    return false;
                }
                while (!password.empty() && (password.back() == '\r' || password.back() == '\n'))
                {
                    password.pop_back(); // a line ending from a Windows pipe is not part of the password
                }
                return true;
            }
        } // namespace

        int RunServiceMode()
        {
            std::string error;
            if (!RunAsService(error))
            {
                printf("Could not run as a service: %s\n", error.c_str());
                return 1;
            }
            return 0;
        }

        int RunSessionMode(const std::string& dataDirectory, unsigned consoleSession, bool useTestPattern, int port)
        {
            SessionHostOptions options;
            options.dataDirectory = dataDirectory;
            options.consoleSession = consoleSession;
            options.useTestPattern = useTestPattern;
            options.portOverride = port;
            return RunSessionHost(options);
        }

        int RunSetPasswordMode(const std::string& dataDirectory)
        {
            std::string directory = dataDirectory.empty() ? GetDefaultDataDirectory() : dataDirectory;
            std::string password;
            if (!ReadPassword(password))
            {
                printf("No password was given on standard input.\n");
                return 2;
            }
            if (password.empty())
            {
                printf("The password can't be empty: that would let anyone connect.\n");
                return 2;
            }
            std::string hash = HashPassword(password);
            if (hash.empty())
            {
                printf("Could not get secure random numbers to hash the password.\n");
                return 1;
            }
            Settings settings;
            std::string error;
            if (!LoadServiceConfig(directory, settings, error))
            {
                printf("Could not read the settings in %s: %s\n", directory.c_str(), error.c_str());
                return 1;
            }
            settings.passwordHash = hash;
            if (!SaveServiceConfig(directory, settings, error))
            {
                printf("Could not save the password: %s\n", error.c_str());
                return 1;
            }
            printf("The access password was saved in %s\n", GetConfigPath(directory).c_str());
            return 0;
        }

        int RunControlMode(const std::string& command, unsigned viewerId)
        {
            std::string error;
            std::unique_ptr<Wire::Transport> pipe = Control::ConnectToControlPipe(error);
            if (!pipe)
            {
                printf("Could not reach the server: %s\n", error.c_str());
                return 1;
            }
            Control::ControlClient client(*pipe);
            if (!client.Connect("command line", error))
            {
                printf("Could not talk to the server: %s\n", error.c_str());
                return 1;
            }
            std::string text;
            Control::ResultCode code = Control::ResultCode::Ok;
            if (command == "status")
            {
                Control::ServerStatus status;
                if (!client.RefreshStatus(status, error))
                {
                    printf("Could not read the status: %s\n", error.c_str());
                    return 1;
                }
                printf("Marconi Server %s, session %u, port %u, desktop '%s'\n", status.serverVersion.c_str(),
                       static_cast<unsigned>(status.consoleSession), static_cast<unsigned>(status.port),
                       status.desktop.c_str());
                printf("Password %s; lock on disconnect %s\n", status.passwordSet ? "set" : "NOT set",
                       status.lockOnDisconnect ? "on" : "off");
                if (status.viewers.empty())
                {
                    printf("Nobody is connected.\n");
                }
                for (const Control::ViewerInfo& viewer : status.viewers)
                {
                    printf("Viewer %u: %s, connected for %u s\n", static_cast<unsigned>(viewer.id),
                           viewer.name.c_str(), static_cast<unsigned>(viewer.connectedSeconds));
                }
                return 0;
            }
            if (command == "disconnect")
            {
                code = client.DisconnectViewer(viewerId, text);
            }
            else if (command == "set-password")
            {
                std::string password;
                if (!ReadPassword(password) || password.empty())
                {
                    printf("Give the new password on standard input (it can't be empty).\n");
                    return 2;
                }
                code = client.SetPasswordHash(HashPassword(password), text);
            }
            else
            {
                printf("Unknown control command: %s\n", command.c_str());
                return 2;
            }
            if (code == Control::ResultCode::Ok)
            {
                printf("Done.\n");
                return 0;
            }
            printf("%s\n", text.empty() ? "The server could not do that." : text.c_str());
            return code == Control::ResultCode::NotAdministrator ? 3 : 1;
        }

        int RunInstallServiceMode(bool startNow)
        {
            std::string error;
            if (!InstallService(startNow, error))
            {
                printf("Could not install the service: %s\n", error.c_str());
                return 1;
            }
            printf("The Marconi Server service is installed%s.\n", startNow ? " and started" : "");
            return 0;
        }

        int RunRemoveServiceMode()
        {
            std::string error;
            if (!RemoveService(error))
            {
                printf("Could not remove the service: %s\n", error.c_str());
                return 1;
            }
            printf("The Marconi Server service is removed.\n");
            return 0;
        }
    } // namespace Server
} // namespace Marconi
