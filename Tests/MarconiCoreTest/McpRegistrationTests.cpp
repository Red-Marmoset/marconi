// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for registering MarconiMcp with AI tools: the commands sent to Claude Code and Codex and how their replies
// are read, with a stand-in that plays back what the real tools print

#include "TestSupport.h"

#include <MarconiMcp/Registration.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // What the real tools printed (Claude Code 2.1.323, Codex 0.169.0, 2026-11-13).
        const char* const kClaudeGet = "marconi:\n"
                                       "  Scope: User config (available in all your projects)\n"
                                       "  Status: \xe2\x88\x9a Connected\n"
                                       "  Type: stdio\n"
                                       "  Command: /opt/marconi/MarconiMcp\n"
                                       "  Args:\n"
                                       "  Environment:\n";
        const char* const kCodexGet = "marconi\n"
                                      "  enabled: true\n"
                                      "  transport: stdio\n"
                                      "  command: C:\\Program Files\\Marconi\\MarconiMcp.exe\n"
                                      "  args: -\n";

        // Records each command and answers from a script of results, in order.
        struct FakeRunner
        {
            std::vector<std::vector<std::string>> calls;
            std::vector<Process::Result> answers;

            Mcp::Runner Get()
            {
                return [this](const std::vector<std::string>& arguments, int) {
                    calls.push_back(arguments);
                    if (answers.empty())
                    {
                        Process::Result ok;
                        ok.started = true;
                        ok.exitCode = 0;
                        return ok;
                    }
                    Process::Result next = answers.front();
                    answers.erase(answers.begin());
                    return next;
                };
            }
        };

        Process::Result Finished(int exitCode, const std::string& output)
        {
            Process::Result result;
            result.started = true;
            result.exitCode = exitCode;
            result.output = output;
            return result;
        }

        // An app list where claude-code and codex are runnable files in a scratch folder (claude-desktop isn't).
        struct Tools
        {
            std::filesystem::path folder = std::filesystem::temp_directory_path() / "MarconiRegistrationTest";
            std::string claude;
            std::string codex;
            AppDetect::Catalog catalog;

            Tools()
            {
                std::filesystem::remove_all(folder);
                std::filesystem::create_directories(folder);
                claude = MakeProgram("claude");
                codex = MakeProgram("codex");
                std::string json = std::string("{\"apps\": [") + "{\"id\": \"claude-code\", \"" +
                                   AppDetect::PlatformKey() + "\": {\"files\": [\"" + Escaped(claude) + "\"]}}," +
                                   "{\"id\": \"codex\", \"" + AppDetect::PlatformKey() + "\": {\"files\": [\"" +
                                   Escaped(codex) + "\"]}}," + "{\"id\": \"claude-desktop\"}]}";
                std::string error;
                CHECK(catalog.LoadText(json, error));
            }

            ~Tools() { std::filesystem::remove_all(folder); }

            std::string MakeProgram(const std::string& name)
            {
#if defined(_WIN32)
                const std::filesystem::path path = folder / (name + ".exe");
                std::ofstream(path) << "";
#else
                const std::filesystem::path path = folder / name;
                std::ofstream(path) << "#!/bin/sh\n";
                std::filesystem::permissions(path, std::filesystem::perms::owner_all);
#endif
                return path.string();
            }

            static std::string Escaped(const std::string& text)
            {
                std::string out;
                for (char c : text)
                {
                    out += c;
                    if (c == '\\')
                    {
                        out += '\\';
                    }
                }
                return out;
            }
        };
    } // namespace

    void RunMcpRegistrationTests()
    {
        RunTest("MCP registration: the commands each tool is given", []() {
            using Mcp::AiTool;
            using Mcp::Registrar;
            CHECK(Registrar::AddCommand(AiTool::ClaudeCode, "claude", "/m") ==
                  std::vector<std::string>({"claude", "mcp", "add", "--scope", "user", "marconi", "--", "/m"}));
            CHECK(Registrar::RemoveCommand(AiTool::ClaudeCode, "claude") ==
                  std::vector<std::string>({"claude", "mcp", "remove", "--scope", "user", "marconi"}));
            CHECK(Registrar::AddCommand(AiTool::Codex, "codex", "C:\\M M\\MarconiMcp.exe") ==
                  std::vector<std::string>({"codex", "mcp", "add", "marconi", "--", "C:\\M M\\MarconiMcp.exe"}));
            CHECK(Registrar::RemoveCommand(AiTool::Codex, "codex") ==
                  std::vector<std::string>({"codex", "mcp", "remove", "marconi"}));
            CHECK(Registrar::GetCommand(AiTool::Codex, "codex") ==
                  std::vector<std::string>({"codex", "mcp", "get", "marconi"}));
        });

        RunTest("MCP registration: reading which program an entry starts", []() {
            CHECK(Mcp::Registrar::ParseRegisteredCommand(kClaudeGet) == "/opt/marconi/MarconiMcp");
            CHECK(Mcp::Registrar::ParseRegisteredCommand(kCodexGet) == "C:\\Program Files\\Marconi\\MarconiMcp.exe");
            CHECK(Mcp::Registrar::ParseRegisteredCommand("No MCP server named \"marconi\".").empty());
            CHECK(Mcp::FindAiTool("codex") && Mcp::FindAiTool("codex")->tool == Mcp::AiTool::Codex);
            CHECK(Mcp::FindAiTool("chatgpt") == nullptr);
        });

        RunTest("MCP registration: Claude Code gets the old entry removed, then the new one added", []() {
            Tools tools;
            FakeRunner runner;
            Mcp::Registrar registrar(tools.catalog, "/opt/marconi/MarconiMcp", "", runner.Get());
            std::string message;
            CHECK(registrar.Register(Mcp::AiTool::ClaudeCode, message));
            CHECK(runner.calls.size() == 2);
            CHECK(runner.calls[0] == Mcp::Registrar::RemoveCommand(Mcp::AiTool::ClaudeCode, tools.claude));
            CHECK(runner.calls[1] ==
                  Mcp::Registrar::AddCommand(Mcp::AiTool::ClaudeCode, tools.claude, "/opt/marconi/MarconiMcp"));
            CHECK(message == "Added Marconi to Claude Code.");

            // Codex replaces an entry by itself, so it only gets the add.
            runner.calls.clear();
            CHECK(registrar.Register(Mcp::AiTool::Codex, message));
            CHECK(runner.calls.size() == 1 && runner.calls[0][2] == "add");
        });

        RunTest("MCP registration: a refusal says what the tool said", []() {
            Tools tools;
            FakeRunner runner;
            runner.answers = {Finished(1, "No MCP server named \"marconi\""), Finished(1, "something went wrong")};
            Mcp::Registrar registrar(tools.catalog, "/m", "", runner.Get());
            std::string message;
            CHECK(!registrar.Register(Mcp::AiTool::ClaudeCode, message));
            CHECK(message.find("something went wrong") != std::string::npos);

            Process::Result notStarted;
            notStarted.error = "Couldn't start claude: denied";
            runner.answers = {notStarted, notStarted};
            CHECK(!registrar.Register(Mcp::AiTool::ClaudeCode, message));
            CHECK(message == "Couldn't start claude: denied");
        });

        RunTest("MCP registration: status asks the tool", []() {
            Tools tools;
            FakeRunner runner;
            runner.answers = {Finished(0, kClaudeGet), Finished(1, "Error: No MCP server named 'marconi' found.")};
            Mcp::Registrar registrar(tools.catalog, "/opt/marconi/MarconiMcp", "", runner.Get());
            Mcp::ToolStatus status = registrar.Status(Mcp::AiTool::ClaudeCode);
            CHECK(status.found && status.toolPath == tools.claude);
            CHECK(status.registrationKnown && status.registered);
            CHECK(status.registeredCommand == "/opt/marconi/MarconiMcp");
            CHECK(Mcp::PointsAt(status, "/opt/marconi/MarconiMcp"));
            CHECK(!Mcp::PointsAt(status, "/somewhere/else/MarconiMcp"));
            CHECK(runner.calls[0] == Mcp::Registrar::GetCommand(Mcp::AiTool::ClaudeCode, tools.claude));

            status = registrar.Status(Mcp::AiTool::Codex);
            CHECK(status.found && status.registrationKnown && !status.registered);

            // Claude Desktop can't be asked, and here it isn't installed.
            status = registrar.Status(Mcp::AiTool::ClaudeDesktop);
            CHECK(!status.found && !status.registrationKnown);
        });

        RunTest("MCP registration: removing, and tools that aren't there", []() {
            Tools tools;
            FakeRunner runner;
            Mcp::Registrar registrar(tools.catalog, "/m", "", runner.Get());
            std::string message;
            runner.answers = {Finished(0, kCodexGet), Finished(0, "Removed global MCP server 'marconi'.")};
            CHECK(registrar.Unregister(Mcp::AiTool::Codex, message));
            CHECK(runner.calls.size() == 2 && runner.calls[1] == Mcp::Registrar::RemoveCommand(Mcp::AiTool::Codex,
                                                                                               tools.codex));
            // Not registered: nothing to remove, and that's fine.
            runner.calls.clear();
            runner.answers = {Finished(1, "Error: No MCP server named 'marconi' found.")};
            CHECK(registrar.Unregister(Mcp::AiTool::Codex, message));
            CHECK(runner.calls.size() == 1);

            CHECK(!registrar.Register(Mcp::AiTool::ClaudeDesktop, message));
            CHECK(message.find("isn't installed") != std::string::npos);
            CHECK(!registrar.Unregister(Mcp::AiTool::ClaudeDesktop, message));
            CHECK(message.find("Claude Desktop's settings") != std::string::npos);
        });
    }
} // namespace MarconiTest
