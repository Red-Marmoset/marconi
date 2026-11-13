// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Telling AI tools about MarconiMcp: finding Claude Code, Codex and Claude Desktop, and adding, removing and checking
// MarconiMcp's entry in each, always through the tool's own commands

#pragma once

#include <AppDetect/AppDetect.h>
#include <Process/Process.h>

#include <functional>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        enum class AiTool
        {
            ClaudeCode,
            Codex,
            ClaudeDesktop,
        };

        struct AiToolInfo
        {
            AiTool tool;
            const char* id;   // as Apps.json and the command line spell it: "claude-code"
            const char* name; // for people: "Claude Code"
        };

        // Every supported tool, in the order windows and lists show them.
        const std::vector<AiToolInfo>& AiTools();
        const AiToolInfo* FindAiTool(const std::string& id);
        const AiToolInfo& InfoFor(AiTool tool);

        // The name MarconiMcp is registered under in every tool.
        constexpr const char* kServerName = "marconi";

        struct ToolStatus
        {
            bool found = false;         // the tool is installed
            std::string toolPath;       // its program (or folder), when found
            bool registrationKnown = false; // false for Claude Desktop, which can't be asked from outside
            bool registered = false;    // MarconiMcp has an entry in it
            std::string registeredCommand; // what that entry starts, as the tool reports it
        };

        // Runs a program; Process::Run, or a stand-in in tests.
        using Runner = std::function<Process::Result(const std::vector<std::string>& arguments, int timeoutMs)>;

        class Registrar
        {
        public:
            // `apps` is the engine's list (Apps.json); `mcpPath` is the MarconiMcp program to register; `extension`
            // the Claude Desktop extension file (marconi.mcpb), if there is one.
            Registrar(const AppDetect::Catalog& apps, std::string mcpPath, std::string extension,
                      Runner runner = Process::Run);

            ToolStatus Status(AiTool tool) const;
            // Adds MarconiMcp's entry, replacing an earlier one. For Claude Desktop, opens the extension file, which
            // shows Claude's own install screen: whether the user confirms can't be seen from here.
            bool Register(AiTool tool, std::string& message) const;
            bool Unregister(AiTool tool, std::string& message) const;

            // The commands used, for tests and messages. `command` is the tool's program.
            static std::vector<std::string> AddCommand(AiTool tool, const std::string& command,
                                                       const std::string& mcpPath);
            static std::vector<std::string> RemoveCommand(AiTool tool, const std::string& command);
            static std::vector<std::string> GetCommand(AiTool tool, const std::string& command);
            // The "command:" line of `claude mcp get` / `codex mcp get` output; empty if there's none.
            static std::string ParseRegisteredCommand(const std::string& output);

        private:
            bool FindTool(AiTool tool, std::string& path) const;

            const AppDetect::Catalog& m_apps;
            std::string m_mcpPath;
            std::string m_extension;
            Runner m_runner;
        };

        // Whether the tool's entry starts this program (not an older copy somewhere else).
        bool PointsAt(const ToolStatus& status, const std::string& program);

        // Where the engine's Apps.json is: beside this program (Windows), in ../share/marconi (Linux package),
        // ../Resources (macOS bundle), else the engine folder ($UNA_ENGINE_PATH, or the default C:\code\engine) for
        // development builds. Empty if none exists.
        std::string FindAppsJson();

        // This program's path: MarconiMcp, as it's running.
        std::string ThisProgram();

        // The Claude Desktop extension beside this program (marconi.mcpb), or empty if there's none.
        std::string FindExtension();
    } // namespace Mcp
} // namespace Marconi
