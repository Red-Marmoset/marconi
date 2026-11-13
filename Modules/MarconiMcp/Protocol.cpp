// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Model Context Protocol's messages: answering an AI tool's requests, one line of JSON at a time

#include <MarconiMcp/Protocol.h>

#include <algorithm>
#include <utility>

namespace Marconi
{
    namespace Mcp
    {
        const char* const kModernVersion = "2026-07-28";
        const std::vector<std::string> kLegacyVersions = {"2025-11-25", "2025-06-18", "2025-03-26"};

        namespace
        {
            const char* const kMetaProtocolVersion = "io.modelcontextprotocol/protocolVersion";
            const char* const kMetaClientCapabilities = "io.modelcontextprotocol/clientCapabilities";
            const char* const kMetaServerInfo = "io.modelcontextprotocol/serverInfo";

            nlohmann::json ErrorReply(const nlohmann::json& id, int code, const std::string& message,
                                      const nlohmann::json& data = nullptr)
            {
                nlohmann::json error = {{"code", code}, {"message", message}};
                if (!data.is_null())
                {
                    error["data"] = data;
                }
                return {{"jsonrpc", "2.0"}, {"id", id}, {"error", error}};
            }

            nlohmann::json AllVersions()
            {
                nlohmann::json versions = nlohmann::json::array({kModernVersion});
                for (const std::string& version : kLegacyVersions)
                {
                    versions.push_back(version);
                }
                return versions;
            }

            // A thrown request error: turned into a JSON-RPC error reply.
            struct RequestError
            {
                int code;
                std::string message;
                nlohmann::json data;
            };
        } // namespace

        void ToolResult::AddText(const std::string& text)
        {
            content.push_back({{"type", "text"}, {"text", text}});
        }

        void ToolResult::AddImage(const std::string& base64Data, const std::string& mimeType)
        {
            content.push_back({{"type", "image"}, {"data", base64Data}, {"mimeType", mimeType}});
        }

        ToolResult ToolResult::Error(const std::string& message)
        {
            ToolResult result;
            result.AddText(message);
            result.isError = true;
            return result;
        }

        Server::Server(ToolSet& tools, ServerInfo info) : m_tools(tools), m_info(std::move(info)) {}

        std::string Server::HandleMessage(const std::string& line)
        {
            nlohmann::json reply;
            nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
            if (message.is_discarded())
            {
                reply = ErrorReply(nullptr, kParseError, "Parse error");
            }
            else if (!message.is_object())
            {
                reply = ErrorReply(nullptr, kInvalidRequest, "Invalid request: expected one JSON object");
            }
            else if (!message.contains("method"))
            {
                // A reply from the client (we never send requests) or something malformed: nothing to answer.
                return std::string();
            }
            else
            {
                const nlohmann::json& method = message["method"];
                const bool isRequest = message.contains("id");
                const nlohmann::json id = isRequest ? message["id"] : nlohmann::json();
                if (!method.is_string())
                {
                    if (!isRequest)
                    {
                        return std::string();
                    }
                    reply = ErrorReply(id, kInvalidRequest, "Invalid request: method must be a string");
                }
                else if (!isRequest)
                {
                    // Notifications (initialized, cancelled, ...) need no answer. Calls are handled one at a time
                    // and finish on their own, so there is nothing in flight to cancel.
                    return std::string();
                }
                else
                {
                    const nlohmann::json params = message.value("params", nlohmann::json::object());
                    try
                    {
                        reply = HandleRequest(id, method.get<std::string>(), params);
                    }
                    catch (const RequestError& error)
                    {
                        reply = ErrorReply(id, error.code, error.message, error.data);
                    }
                }
            }
            // Escaped, so no message contains a raw newline; text that isn't valid UTF-8 is replaced, not refused.
            return reply.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        nlohmann::json Server::HandleRequest(const nlohmann::json& id, const std::string& method,
                                             const nlohmann::json& params)
        {
            if (!params.is_object())
            {
                throw RequestError{kInvalidParams, "Invalid params: expected an object", nullptr};
            }

            // 2026-07-28 and later: each request names its revision in _meta. Without it, the older revisions'
            // rules apply (the client started with `initialize`).
            bool modern = false;
            const nlohmann::json meta = params.value("_meta", nlohmann::json::object());
            if (meta.is_object() && meta.contains(kMetaProtocolVersion))
            {
                const nlohmann::json& requested = meta[kMetaProtocolVersion];
                if (!requested.is_string() || requested.get<std::string>() != kModernVersion)
                {
                    throw RequestError{kUnsupportedProtocolVersion, "Unsupported protocol version",
                                       {{"supported", AllVersions()}, {"requested", requested}}};
                }
                if (!meta.contains(kMetaClientCapabilities))
                {
                    throw RequestError{kInvalidParams, "Invalid params: _meta needs the client's capabilities",
                                       nullptr};
                }
                modern = true;
            }

            nlohmann::json result;
            if (method == "initialize" && !modern)
            {
                result = Initialize(params);
            }
            else if (method == "server/discover")
            {
                result = Discover();
            }
            else if (method == "ping" && !modern)
            {
                result = nlohmann::json::object();
            }
            else if (method == "tools/list")
            {
                result = ListTools();
            }
            else if (method == "tools/call")
            {
                const nlohmann::json name = params.value("name", nlohmann::json());
                if (!name.is_string())
                {
                    throw RequestError{kInvalidParams, "Invalid params: tools/call needs the tool's name", nullptr};
                }
                nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
                if (arguments.is_null())
                {
                    arguments = nlohmann::json::object();
                }
                if (!arguments.is_object())
                {
                    throw RequestError{kInvalidParams, "Invalid params: arguments must be an object", nullptr};
                }
                ToolResult toolResult;
                if (!m_tools.CallTool(name.get<std::string>(), arguments, toolResult))
                {
                    throw RequestError{kInvalidParams, "Unknown tool: " + name.get<std::string>(), nullptr};
                }
                result = {{"content", toolResult.content}, {"isError", toolResult.isError}};
            }
            else
            {
                throw RequestError{kMethodNotFound, "Method not found: " + method, nullptr};
            }

            if (modern)
            {
                result["resultType"] = "complete";
                if (!result.contains("_meta"))
                {
                    result["_meta"] = {{kMetaServerInfo, ServerInfoJson()}};
                }
            }
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        }

        nlohmann::json Server::Initialize(const nlohmann::json& params) const
        {
            // The client's revision if we speak it, otherwise our newest older-style one.
            std::string version = kLegacyVersions.front();
            const nlohmann::json requested = params.value("protocolVersion", nlohmann::json());
            if (requested.is_string() &&
                std::find(kLegacyVersions.begin(), kLegacyVersions.end(), requested.get<std::string>()) !=
                    kLegacyVersions.end())
            {
                version = requested.get<std::string>();
            }
            nlohmann::json result = {{"protocolVersion", version},
                                     {"capabilities", {{"tools", nlohmann::json::object()}}},
                                     {"serverInfo", ServerInfoJson()}};
            if (!m_info.instructions.empty())
            {
                result["instructions"] = m_info.instructions;
            }
            return result;
        }

        nlohmann::json Server::Discover() const
        {
            nlohmann::json result = {{"supportedVersions", AllVersions()},
                                     {"capabilities", {{"tools", nlohmann::json::object()}}},
                                     {"_meta", {{kMetaServerInfo, ServerInfoJson()}}}};
            if (!m_info.instructions.empty())
            {
                result["instructions"] = m_info.instructions;
            }
            return result;
        }

        nlohmann::json Server::ListTools() const
        {
            nlohmann::json tools = nlohmann::json::array();
            for (const ToolDefinition& tool : m_tools.ListTools())
            {
                nlohmann::json entry = {{"name", tool.name},
                                        {"description", tool.description},
                                        {"inputSchema", tool.inputSchema}};
                if (!tool.title.empty())
                {
                    entry["title"] = tool.title;
                }
                entry["annotations"] = {{"readOnlyHint", tool.readOnly}};
                tools.push_back(entry);
            }
            return {{"tools", tools}};
        }

        nlohmann::json Server::ServerInfoJson() const
        {
            nlohmann::json info = {{"name", m_info.name}, {"version", m_info.version}};
            if (!m_info.title.empty())
            {
                info["title"] = m_info.title;
            }
            return info;
        }
    } // namespace Mcp
} // namespace Marconi
