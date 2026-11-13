// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The settings the service and its session helper share; see ServiceConfig.h

#include "ServiceConfig.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::string Trim(const std::string& text)
            {
                size_t begin = text.find_first_not_of(" \t\r\n");
                if (begin == std::string::npos)
                {
                    return std::string();
                }
                size_t end = text.find_last_not_of(" \t\r\n");
                return text.substr(begin, end - begin + 1);
            }
        } // namespace

        std::string GetDefaultDataDirectory()
        {
#ifdef _WIN32
            char* programData = nullptr;
            size_t length = 0;
            std::string base = "C:\\ProgramData";
            if (_dupenv_s(&programData, &length, "ProgramData") == 0 && programData != nullptr)
            {
                base = programData;
                free(programData);
            }
            return base + "\\Marconi Server";
#else
            return "/var/lib/marconi-server";
#endif
        }

        std::string GetConfigPath(const std::string& dataDirectory)
        {
            return (std::filesystem::path(dataDirectory) / "server.conf").string();
        }

        std::string GetCertificatePath(const std::string& dataDirectory)
        {
            return (std::filesystem::path(dataDirectory) / "server.crt").string();
        }

        std::string GetPrivateKeyPath(const std::string& dataDirectory)
        {
            return (std::filesystem::path(dataDirectory) / "server.key").string();
        }

        bool LoadServiceConfig(const std::string& dataDirectory, Settings& settings, std::string& error)
        {
            std::ifstream file(GetConfigPath(dataDirectory));
            if (!file)
            {
                return true; // no file yet: the defaults stand
            }
            std::string line;
            int lineNumber = 0;
            while (std::getline(file, line))
            {
                ++lineNumber;
                line = Trim(line);
                if (line.empty() || line[0] == '#')
                {
                    continue;
                }
                size_t equals = line.find('=');
                if (equals == std::string::npos)
                {
                    error = "line " + std::to_string(lineNumber) + " of the settings file has no '='";
                    return false;
                }
                std::string key = Trim(line.substr(0, equals));
                std::string value = Trim(line.substr(equals + 1));
                if (key == "port")
                {
                    char* end = nullptr;
                    long port = std::strtol(value.c_str(), &end, 10);
                    if (value.empty() || *end != '\0' || port < 0 || port > 65535)
                    {
                        error = "the port in the settings file is not a number from 0 to 65535";
                        return false;
                    }
                    settings.port = static_cast<uint16_t>(port);
                }
                else if (key == "name")
                {
                    settings.serverName = value;
                }
                else if (key == "password-hash")
                {
                    settings.passwordHash = value;
                }
                else if (key == "lock-on-disconnect")
                {
                    if (value != "yes" && value != "no")
                    {
                        error = "lock-on-disconnect in the settings file must be yes or no";
                        return false;
                    }
                    settings.lockOnDisconnect = value == "yes";
                }
                // Any other key is ignored, so a newer build's file still loads in an older one.
            }
            return true;
        }

        bool SaveServiceConfig(const std::string& dataDirectory, const Settings& settings, std::string& error)
        {
            std::error_code code;
            std::filesystem::create_directories(dataDirectory, code);
            if (code)
            {
                error = "could not create the settings folder: " + code.message();
                return false;
            }
            std::string path = GetConfigPath(dataDirectory);
            std::string temporary = path + ".tmp";
            {
                std::ofstream file(temporary, std::ios::trunc);
                if (!file)
                {
                    error = "could not write the settings file";
                    return false;
                }
                file << "# Marconi Server settings. The password is stored as a hash, never as typed.\n";
                file << "port=" << settings.port << "\n";
                if (!settings.serverName.empty())
                {
                    file << "name=" << settings.serverName << "\n";
                }
                file << "password-hash=" << settings.passwordHash << "\n";
                file << "lock-on-disconnect=" << (settings.lockOnDisconnect ? "yes" : "no") << "\n";
                file.flush();
                if (!file)
                {
                    error = "could not write the settings file";
                    return false;
                }
            }
            // Replace in one step so a crash never leaves half a file (and so the password hash is never lost).
            std::filesystem::rename(temporary, path, code);
            if (code)
            {
                error = "could not replace the settings file: " + code.message();
                return false;
            }
            return true;
        }
    } // namespace Server
} // namespace Marconi
