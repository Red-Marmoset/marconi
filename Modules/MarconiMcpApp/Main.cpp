// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiMcp: the program AI tools start to drive Marconi, talking the Model Context Protocol on its standard input
// and output

#include <MarconiCore/Version.h>
#include <MarconiMcp/LogFiles.h>
#include <MarconiMcp/Protocol.h>
#include <MarconiMcp/Registration.h>
#include <MarconiMcp/Tools.h>

#include <MarconiCore/Net/Discovery.h>

#include "StoreComputers.h"

#include <MarconiConnections/ConnectionStore.h>

#include <wx/app.h>
#include <wx/init.h>

#include <json/single_include/nlohmann/json.hpp>

#include <Log/Log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    void PrintUsage()
    {
        std::printf("Marconi for AI tools %s\n\n", Marconi::GetVersionString());
        std::printf("Usage: MarconiMcp [options]\n");
        std::printf("  (no options)   run as an MCP server on standard input and output; AI tools start it this way\n");
        std::printf("  --read-only    offer only the tools that look: no mouse, keyboard or clipboard changes\n");
        std::printf("  --register TOOL     add Marconi to an AI tool: claude-code, codex or claude-desktop\n");
        std::printf("  --unregister TOOL   remove it again\n");
        std::printf("  --status            for each AI tool, one JSON line: installed, where, and whether Marconi\n");
        std::printf("                      is in it\n");
        std::printf("  --show-errors       with --register or --unregister, also show a failure in a message box\n");
        std::printf("  --quiet             with those, print only the answer (the log still goes to the log file)\n");
        std::printf("  --version      print the version and exit\n");
        std::printf("  --help         show this\n");
        std::printf("\nThe password for connections can come from the MARCONI_PASSWORD environment variable.\n");
        std::printf("Logs go to standard error and to %s.\n", Marconi::Mcp::LogDirectory().c_str());
    }

    // Keeps the real standard output for MCP messages only and points standard output at standard error, so
    // anything else that prints (the engine's log writes info messages to standard output) can't corrupt them.
    FILE* TakeStandardOutput()
    {
        std::fflush(stdout);
#ifdef _WIN32
        const int messages = _dup(_fileno(stdout));
        if (messages < 0 || _dup2(_fileno(stderr), _fileno(stdout)) != 0)
        {
            return nullptr;
        }
        _setmode(messages, _O_BINARY); // "\n", not "\r\n"
        _setmode(_fileno(stdin), _O_BINARY);
        return _fdopen(messages, "wb");
#else
        const int messages = dup(fileno(stdout));
        if (messages < 0 || dup2(fileno(stderr), fileno(stdout)) < 0)
        {
            return nullptr;
        }
        return fdopen(messages, "w");
#endif
    }

    // Sends standard output and standard error nowhere, once the answers have their own copy of standard output.
    void SilenceConsole()
    {
        std::fflush(stdout);
        std::fflush(stderr);
#ifdef _WIN32
        const int nothing = _open("NUL", _O_WRONLY);
        if (nothing >= 0)
        {
            _dup2(nothing, _fileno(stdout));
            _dup2(nothing, _fileno(stderr));
            _close(nothing);
        }
#else
        const int nothing = open("/dev/null", O_WRONLY);
        if (nothing >= 0)
        {
            dup2(nothing, fileno(stdout));
            dup2(nothing, fileno(stderr));
            close(nothing);
        }
#endif
    }

    int RunServer(bool readOnly)
    {
        FILE* messages = TakeStandardOutput();
        if (!messages)
        {
            std::fprintf(stderr, "MarconiMcp: couldn't set up standard output\n");
            return 1;
        }
        // Everything printed from here on goes to standard error, which AI tools keep as the server's log, and to
        // a log file beside the viewer's.
        const bool logFile = Marconi::Mcp::StartFileLogging();

        // Marconi's saved connections, read the way the app reads them: wx's base library, no windows.
        wxApp::SetInstance(new wxAppConsole());
        wxInitializer wx;
        MarconiMcpApp::StoreComputers saved;
        const bool haveSaved = wx.IsOk();
        if (haveSaved)
        {
            MarconiUI::UseMarconiSettings();
        }
        else
        {
            LOG_WARNING("Mcp", "Couldn't start wx, so Marconi's saved connections can't be read");
        }

        Marconi::Mcp::ToolOptions toolOptions;
        toolOptions.readOnly = readOnly;
        toolOptions.saved = haveSaved ? &saved : nullptr;
        toolOptions.discover = []() { return Marconi::Net::Discover(1500); };
        if (const char* password = std::getenv("MARCONI_PASSWORD"))
        {
            toolOptions.defaultPassword = password;
        }
        Marconi::Mcp::MarconiTools tools(toolOptions);

        Marconi::Mcp::ServerInfo info;
        info.version = Marconi::GetVersionString();
        info.instructions = Marconi::Mcp::ServerInstructions();
        Marconi::Mcp::Server server(tools, info);

        LOG_INFO("Mcp", "Marconi for AI tools %s started%s", Marconi::GetVersionString(),
                 readOnly ? " (read-only)" : "");
        if (!logFile)
        {
            LOG_WARNING("Mcp", "Couldn't start a log file in %s", Marconi::Mcp::LogDirectory().c_str());
        }
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty())
            {
                continue;
            }
            const std::string reply = server.HandleMessage(line);
            if (!reply.empty())
            {
                std::fwrite(reply.data(), 1, reply.size(), messages);
                std::fputc('\n', messages);
                std::fflush(messages);
            }
        }
        // Standard input closed: the AI tool is done with us.
        LOG_INFO("Mcp", "Standard input closed; stopping");
        tools.GetConnection().Disconnect();
        Log::ShutdownFileLogging();
        std::fclose(messages);
        return 0;
    }

    // Shown when Setup starts registering as the signed-in user and can't see the result itself.
    void ShowError(const std::string& message)
    {
#ifdef _WIN32
        const int length = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), length);
        MessageBoxW(nullptr, wide.c_str(), L"Marconi", MB_OK | MB_ICONWARNING);
#else
        (void)message;
#endif
    }

    bool LoadApps(AppDetect::Catalog& apps)
    {
        const std::string path = Marconi::Mcp::FindAppsJson();
        std::string error;
        if (path.empty() || !apps.LoadFile(path, error))
        {
            std::fprintf(stderr, "MarconiMcp: the list of apps (Apps.json) %s\n",
                         path.empty() ? "isn't beside this program" : error.c_str());
            return false;
        }
        return true;
    }

    // What --status and --register print for the app and Setup to read goes here; logging goes to standard error.
    FILE* g_answers = stdout;

    void Answer(const std::string& line)
    {
        std::fwrite(line.data(), 1, line.size(), g_answers);
        std::fputc('\n', g_answers);
        std::fflush(g_answers);
    }

    int PrintStatus()
    {
        AppDetect::Catalog apps;
        if (!LoadApps(apps))
        {
            return 1;
        }
        const std::string program = Marconi::Mcp::ThisProgram();
        const Marconi::Mcp::Registrar registrar(apps, program, Marconi::Mcp::FindExtension());
        for (const Marconi::Mcp::AiToolInfo& info : Marconi::Mcp::AiTools())
        {
            const Marconi::Mcp::ToolStatus status = registrar.Status(info.tool);
            nlohmann::json line = {{"tool", info.id}, {"name", info.name}, {"found", status.found}};
            if (status.found)
            {
                line["path"] = status.toolPath;
            }
            if (status.registrationKnown)
            {
                line["registered"] = status.registered;
                if (status.registered)
                {
                    line["command"] = status.registeredCommand;
                    line["current"] = Marconi::Mcp::PointsAt(status, program);
                }
            }
            Answer(line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
        return 0;
    }

    int ChangeRegistration(const std::string& id, bool add, bool showErrors)
    {
        const Marconi::Mcp::AiToolInfo* info = Marconi::Mcp::FindAiTool(id);
        if (!info)
        {
            std::fprintf(stderr, "Unknown AI tool \"%s\": use claude-code, codex or claude-desktop\n", id.c_str());
            return 2;
        }
        Marconi::Mcp::StartFileLogging();
        AppDetect::Catalog apps;
        std::string message;
        bool done = false;
        if (!LoadApps(apps))
        {
            message = "Marconi's list of AI tools is missing; reinstalling Marconi should fix it.";
        }
        else
        {
            const Marconi::Mcp::Registrar registrar(apps, Marconi::Mcp::ThisProgram(), Marconi::Mcp::FindExtension());
            done = add ? registrar.Register(info->tool, message) : registrar.Unregister(info->tool, message);
        }
        Answer(message);
        if (!done)
        {
            LOG_WARNING("Mcp", "%s %s: %s", add ? "Adding Marconi to" : "Removing Marconi from", info->name,
                        message.c_str());
            if (showErrors)
            {
                ShowError(std::string(add ? "Marconi couldn't be added to " : "Marconi couldn't be removed from ") +
                          info->name + ".\n\n" + message);
            }
        }
        Log::ShutdownFileLogging();
        return done ? 0 : 1;
    }
} // namespace

int main(int argc, char** argv)
{
    bool readOnly = false;
    bool showErrors = false;
    bool quiet = false;
    std::string registerTool;
    std::string unregisterTool;
    bool status = false;
    for (int i = 1; i < argc; ++i)
    {
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(argv[i], "--read-only") == 0)
        {
            readOnly = true;
        }
        else if (std::strcmp(argv[i], "--register") == 0 && hasValue)
        {
            registerTool = argv[++i];
        }
        else if (std::strcmp(argv[i], "--unregister") == 0 && hasValue)
        {
            unregisterTool = argv[++i];
        }
        else if (std::strcmp(argv[i], "--status") == 0)
        {
            status = true;
        }
        else if (std::strcmp(argv[i], "--show-errors") == 0)
        {
            showErrors = true;
        }
        else if (std::strcmp(argv[i], "--quiet") == 0)
        {
            quiet = true;
        }
        else if (std::strcmp(argv[i], "--version") == 0)
        {
            std::printf("%s\n", Marconi::GetVersionString());
            return 0;
        }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            PrintUsage();
            return 0;
        }
        else
        {
            std::fprintf(stderr, "Unknown option: %s (try --help)\n", argv[i]);
            return 2;
        }
    }
    if (!registerTool.empty() || !unregisterTool.empty() || status)
    {
        if (FILE* answers = TakeStandardOutput())
        {
            g_answers = answers;
            if (quiet)
            {
                SilenceConsole();
            }
        }
    }
    if (!registerTool.empty() || !unregisterTool.empty())
    {
        return ChangeRegistration(registerTool.empty() ? unregisterTool : registerTool, !registerTool.empty(),
                                  showErrors);
    }
    if (status)
    {
        return PrintStatus();
    }
    return RunServer(readOnly);
}
