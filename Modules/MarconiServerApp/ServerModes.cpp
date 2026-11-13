// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The command-line modes the service setup needs; see ServerModes.h

#include "ServerModes.h"

#include <MarconiServerCore/PasswordHash.h>
#include <MarconiServerCore/ServiceConfig.h>
#include <MarconiServerCore/ServiceHost.h>
#include <MarconiServerCore/SessionHost.h>

#include <cstdio>
#include <iostream>

namespace Marconi
{
    namespace Server
    {
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
            if (!std::getline(std::cin, password))
            {
                printf("No password was given on standard input.\n");
                return 2;
            }
            while (!password.empty() && (password.back() == '\r' || password.back() == '\n'))
            {
                password.pop_back(); // a line ending from a Windows pipe is not part of the password
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
