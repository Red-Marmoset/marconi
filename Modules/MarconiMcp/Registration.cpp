// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Telling AI tools about MarconiMcp: finding Claude Code, Codex and Claude Desktop, and adding, removing and checking
// MarconiMcp's entry in each, always through the tool's own commands

#include <MarconiMcp/Registration.h>

#include <Log/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            // `claude mcp add` and friends talk to no server, but can be slow on a first run.
            const int kCommandTimeoutMs = 60000;

            std::string Trim(const std::string& text)
            {
                const size_t start = text.find_first_not_of(" \t\r\n");
                if (start == std::string::npos)
                {
                    return std::string();
                }
                const size_t end = text.find_last_not_of(" \t\r\n");
                return text.substr(start, end - start + 1);
            }

            // The last few lines a command printed, for a message.
            std::string Tail(const std::string& output)
            {
                std::string text = Trim(output);
                const size_t kMax = 400;
                if (text.size() > kMax)
                {
                    text = "..." + text.substr(text.size() - kMax);
                }
                return text;
            }

            bool Exists(const std::string& path)
            {
                std::error_code error;
                return !path.empty() && std::filesystem::exists(path, error);
            }

            bool SamePath(const std::string& a, const std::string& b)
            {
                std::error_code error;
                if (a == b)
                {
                    return true;
                }
                return Exists(a) && Exists(b) && std::filesystem::equivalent(a, b, error);
            }
        } // namespace

        const std::vector<AiToolInfo>& AiTools()
        {
            static const std::vector<AiToolInfo> tools = {
                {AiTool::ClaudeCode, "claude-code", "Claude Code"},
                {AiTool::Codex, "codex", "Codex"},
                {AiTool::ClaudeDesktop, "claude-desktop", "Claude Desktop"},
            };
            return tools;
        }

        const AiToolInfo* FindAiTool(const std::string& id)
        {
            for (const AiToolInfo& info : AiTools())
            {
                if (id == info.id)
                {
                    return &info;
                }
            }
            return nullptr;
        }

        const AiToolInfo& InfoFor(AiTool tool)
        {
            for (const AiToolInfo& info : AiTools())
            {
                if (info.tool == tool)
                {
                    return info;
                }
            }
            return AiTools().front();
        }

        Registrar::Registrar(const AppDetect::Catalog& apps, std::string mcpPath, std::string extension, Runner runner)
            : m_apps(apps), m_mcpPath(std::move(mcpPath)), m_extension(std::move(extension)),
              m_runner(std::move(runner))
        {
        }

        std::vector<std::string> Registrar::AddCommand(AiTool tool, const std::string& command,
                                                       const std::string& mcpPath)
        {
            if (tool == AiTool::ClaudeCode)
            {
                // User scope: every project of this user's, which is what installing Marconi means.
                return {command, "mcp", "add", "--scope", "user", kServerName, "--", mcpPath};
            }
            return {command, "mcp", "add", kServerName, "--", mcpPath};
        }

        std::vector<std::string> Registrar::RemoveCommand(AiTool tool, const std::string& command)
        {
            if (tool == AiTool::ClaudeCode)
            {
                return {command, "mcp", "remove", "--scope", "user", kServerName};
            }
            return {command, "mcp", "remove", kServerName};
        }

        std::vector<std::string> Registrar::GetCommand(AiTool /*tool*/, const std::string& command)
        {
            return {command, "mcp", "get", kServerName};
        }

        std::string Registrar::ParseRegisteredCommand(const std::string& output)
        {
            // Claude Code prints "  Command: /path", Codex "  command: /path".
            size_t start = 0;
            while (start < output.size())
            {
                size_t end = output.find('\n', start);
                if (end == std::string::npos)
                {
                    end = output.size();
                }
                const std::string line = Trim(output.substr(start, end - start));
                std::string lower = line.substr(0, 8);
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                if (lower == "command:")
                {
                    return Trim(line.substr(8));
                }
                start = end + 1;
            }
            return std::string();
        }

        bool Registrar::FindTool(AiTool tool, std::string& path) const
        {
            AppDetect::Found found;
            if (!m_apps.Locate(InfoFor(tool).id, found))
            {
                return false;
            }
            path = found.path;
            return true;
        }

        ToolStatus Registrar::Status(AiTool tool) const
        {
            ToolStatus status;
            status.found = FindTool(tool, status.toolPath);
            if (!status.found || tool == AiTool::ClaudeDesktop)
            {
                return status;
            }
            const Process::Result result = m_runner(GetCommand(tool, status.toolPath), kCommandTimeoutMs);
            if (!result.started || result.timedOut)
            {
                return status; // couldn't ask: unknown
            }
            status.registrationKnown = true;
            status.registered = result.exitCode == 0;
            if (status.registered)
            {
                status.registeredCommand = ParseRegisteredCommand(result.output);
            }
            return status;
        }

        bool Registrar::Register(AiTool tool, std::string& message) const
        {
            const AiToolInfo& info = InfoFor(tool);
            std::string command;
            if (!FindTool(tool, command))
            {
                message = std::string(info.name) + " isn't installed on this computer.";
                return false;
            }
            if (tool == AiTool::ClaudeDesktop)
            {
                if (!Exists(m_extension))
                {
                    message = "The Claude Desktop extension (marconi.mcpb) isn't beside this program.";
                    return false;
                }
                std::string error;
                if (!Process::OpenWithDefaultApp(m_extension, error))
                {
                    message = error;
                    return false;
                }
                message = "Opened the Marconi extension; confirm the install in Claude Desktop.";
                return true;
            }

            // Claude Code refuses a name that's taken, so the earlier entry goes first (Codex replaces it anyway).
            if (tool == AiTool::ClaudeCode)
            {
                m_runner(RemoveCommand(tool, command), kCommandTimeoutMs);
            }
            const Process::Result result = m_runner(AddCommand(tool, command, m_mcpPath), kCommandTimeoutMs);
            if (!result.started)
            {
                message = result.error;
                return false;
            }
            if (result.timedOut || result.exitCode != 0)
            {
                message = std::string(info.name) + " didn't add Marconi" +
                          (result.timedOut ? " (it didn't finish in time)" : "") +
                          (result.output.empty() ? std::string(".") : ": " + Tail(result.output));
                return false;
            }
            LOG_INFO("Mcp", "Registered %s with %s (%s)", m_mcpPath.c_str(), info.name, command.c_str());
            message = "Added Marconi to " + std::string(info.name) + ".";
            return true;
        }

        bool Registrar::Unregister(AiTool tool, std::string& message) const
        {
            const AiToolInfo& info = InfoFor(tool);
            if (tool == AiTool::ClaudeDesktop)
            {
                message = "Remove Marconi in Claude Desktop's settings, under Extensions.";
                return false;
            }
            std::string command;
            if (!FindTool(tool, command))
            {
                message = std::string(info.name) + " isn't installed on this computer.";
                return false;
            }
            const ToolStatus before = Status(tool);
            if (before.registrationKnown && !before.registered)
            {
                message = "Marconi wasn't in " + std::string(info.name) + ".";
                return true;
            }
            const Process::Result result = m_runner(RemoveCommand(tool, command), kCommandTimeoutMs);
            if (!result.started)
            {
                message = result.error;
                return false;
            }
            if (result.timedOut || result.exitCode != 0)
            {
                message = std::string(info.name) + " didn't remove Marconi" +
                          (result.output.empty() ? std::string(".") : ": " + Tail(result.output));
                return false;
            }
            LOG_INFO("Mcp", "Removed Marconi from %s", info.name);
            message = "Removed Marconi from " + std::string(info.name) + ".";
            return true;
        }

        std::string ThisProgram()
        {
#if defined(_WIN32)
            return Process::ExecutableDirectory() + "\\MarconiMcp.exe";
#else
            // Linux packages install it as marconi-mcp; development builds and the macOS bundle as MarconiMcp.
            const std::string folder = Process::ExecutableDirectory();
            for (const char* name : {"MarconiMcp", "marconi-mcp"})
            {
                const std::string path = folder + "/" + name;
                if (Exists(path))
                {
                    return path;
                }
            }
            return folder + "/MarconiMcp";
#endif
        }

        std::string FindAppsJson()
        {
            const std::string folder = Process::ExecutableDirectory();
            std::vector<std::string> candidates = {
                folder + "/Apps.json",
                folder + "/../share/marconi/Apps.json",
                folder + "/../Resources/Apps.json",
            };
            if (const char* engine = std::getenv("UNA_ENGINE_PATH"))
            {
                candidates.push_back(std::string(engine) + "/Packaging/Apps.json");
            }
#if defined(_WIN32)
            candidates.push_back("C:\\code\\engine\\Packaging\\Apps.json");
#endif
            for (const std::string& candidate : candidates)
            {
                if (Exists(candidate))
                {
                    return candidate;
                }
            }
            return std::string();
        }

        std::string FindExtension()
        {
            const std::string folder = Process::ExecutableDirectory();
            for (const std::string& candidate :
                 {folder + "/marconi.mcpb", folder + "/../share/marconi/marconi.mcpb",
                  folder + "/../Resources/marconi.mcpb"})
            {
                if (Exists(candidate))
                {
                    return candidate;
                }
            }
            return std::string();
        }

        bool PointsAt(const ToolStatus& status, const std::string& program)
        {
            return status.registered && SamePath(status.registeredCommand, program);
        }
    } // namespace Mcp
} // namespace Marconi
