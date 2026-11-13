// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiMcp: the program AI tools start to drive Marconi, talking the Model Context Protocol on its standard input
// and output

#include <MarconiCore/Version.h>
#include <MarconiMcp/Protocol.h>
#include <MarconiMcp/Tools.h>

#include <Log/Log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
    void PrintUsage()
    {
        std::printf("Marconi for AI tools %s\n\n", Marconi::GetVersionString());
        std::printf("Usage: MarconiMcp [options]\n");
        std::printf("  (no options)   run as an MCP server on standard input and output; AI tools start it this way\n");
        std::printf("  --read-only    offer only the tools that look (connect, status, screenshot)\n");
        std::printf("  --version      print the version and exit\n");
        std::printf("  --help         show this\n");
        std::printf("\nThe password for connections can come from the MARCONI_PASSWORD environment variable.\n");
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

    int RunServer(bool readOnly)
    {
        FILE* messages = TakeStandardOutput();
        if (!messages)
        {
            std::fprintf(stderr, "MarconiMcp: couldn't set up standard output\n");
            return 1;
        }
        // Everything printed from here on goes to standard error, which AI tools keep as the server's log.

        Marconi::Mcp::ToolOptions toolOptions;
        toolOptions.readOnly = readOnly;
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
        std::fclose(messages);
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    bool readOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--read-only") == 0)
        {
            readOnly = true;
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
    return RunServer(readOnly);
}
