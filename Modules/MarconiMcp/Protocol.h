// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Model Context Protocol's messages: answering an AI tool's requests, one line of JSON at a time

#pragma once

#include <json/single_include/nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // What a tool returns: content items (text, image) and whether the tool failed. A failure is still a normal
        // result, so the AI reads the message; protocol errors are for malformed requests only.
        struct ToolResult
        {
            nlohmann::json content = nlohmann::json::array();
            bool isError = false;

            void AddText(const std::string& text);
            void AddImage(const std::string& base64Data, const std::string& mimeType);
            static ToolResult Error(const std::string& message);
        };

        struct ToolDefinition
        {
            std::string name;
            std::string title;
            std::string description;
            nlohmann::json inputSchema; // a JSON Schema object
            bool readOnly = false;      // only looks; offered in read-only mode
        };

        // The tools a server offers. Calls arrive one at a time.
        class ToolSet
        {
        public:
            virtual ~ToolSet() = default;
            virtual std::vector<ToolDefinition> ListTools() const = 0;
            // False if there is no tool by that name.
            virtual bool CallTool(const std::string& name, const nlohmann::json& arguments, ToolResult& out) = 0;
        };

        struct ServerInfo
        {
            std::string name = "marconi";
            std::string title = "Marconi";
            std::string version;
            std::string instructions;
        };

        // Protocol revisions this server speaks. 2026-07-28 has no handshake: each request names its revision in
        // params._meta. The older ones start with `initialize`. Both work at once, chosen per request.
        extern const char* const kModernVersion;            // "2026-07-28"
        extern const std::vector<std::string> kLegacyVersions; // newest first

        // JSON-RPC error codes.
        constexpr int kParseError = -32700;
        constexpr int kInvalidRequest = -32600;
        constexpr int kMethodNotFound = -32601;
        constexpr int kInvalidParams = -32602;
        constexpr int kUnsupportedProtocolVersion = -32022;

        class Server
        {
        public:
            Server(ToolSet& tools, ServerInfo info);

            // Handles one message (one line of JSON). Returns the reply as one line without the newline, or empty
            // when no reply is due (notifications, replies from the client).
            std::string HandleMessage(const std::string& line);

        private:
            nlohmann::json HandleRequest(const nlohmann::json& id, const std::string& method,
                                         const nlohmann::json& params);
            nlohmann::json Initialize(const nlohmann::json& params) const;
            nlohmann::json Discover() const;
            nlohmann::json ListTools() const;
            nlohmann::json ServerInfoJson() const;

            ToolSet& m_tools;
            ServerInfo m_info;
        };
    } // namespace Mcp
} // namespace Marconi
