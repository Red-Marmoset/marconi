// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tools an AI tool can call: connecting, screenshots, and (later) the mouse, keyboard and clipboard

#pragma once

#include <MarconiMcp/Connection.h>
#include <MarconiMcp/Protocol.h>
#include <MarconiMcp/SavedComputers.h>

#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        // How the last screenshot relates to the desktop, so the input tools can take its coordinates.
        struct ScreenshotView
        {
            bool valid = false;
            int areaX = 0; // the desktop area shown, in desktop pixels
            int areaY = 0;
            int areaWidth = 0;
            int areaHeight = 0;
            int width = 0; // the picture's size
            int height = 0;
        };

        // Screenshot limits: what every current Claude model takes as it is, and the most a newer one takes.
        constexpr int kDefaultMaxSide = 1568;
        constexpr long long kDefaultMaxPixels = 1150000;
        constexpr int kLargestMaxSide = 2576;
        constexpr long long kLargestMaxPixels = 3750000;

        struct ToolOptions
        {
            bool readOnly = false;        // offer only the tools that look
            std::string defaultPassword;  // used when connect gets none (MARCONI_PASSWORD)
            SavedComputers* saved = nullptr; // Marconi's saved connections, when available
        };

        class MarconiTools : public ToolSet
        {
        public:
            explicit MarconiTools(ToolOptions options);

            std::vector<ToolDefinition> ListTools() const override;
            bool CallTool(const std::string& name, const nlohmann::json& arguments, ToolResult& out) override;

            Connection& GetConnection() { return m_connection; }
            const ScreenshotView& GetView() const { return m_view; }

        private:
            ToolResult Connect(const nlohmann::json& arguments);
            ToolResult ListConnections();
            ToolResult Disconnect();
            ToolResult Status();
            ToolResult Screenshot(const nlohmann::json& arguments);

            // The mouse, keyboard and clipboard (ToolsInput.cpp).
            static void AddInputTools(std::vector<ToolDefinition>& tools);
            bool CallInputTool(const std::string& name, const nlohmann::json& arguments, ToolResult& out);
            ToolResult Click(const nlohmann::json& arguments);
            ToolResult Move(const nlohmann::json& arguments);
            ToolResult Drag(const nlohmann::json& arguments);
            ToolResult Scroll(const nlohmann::json& arguments);
            ToolResult TypeText(const nlohmann::json& arguments);
            ToolResult Key(const nlohmann::json& arguments);
            ToolResult GetClipboard();
            ToolResult SetClipboard(const nlohmann::json& arguments);
            ToolResult WaitForChange(const nlohmann::json& arguments);
            // The session, or an ArgumentError saying why there's none.
            RemoteSession& RequireSession();
            // A point given in the arguments, as desktop pixels; throws ArgumentError if it can't be used.
            void PointArgument(const nlohmann::json& arguments, const char* xKey, const char* yKey, int& x, int& y);
            void PointerTo(RemoteSession& session, int x, int y, uint8_t buttons);
            std::vector<uint32_t> ModifiersArgument(const nlohmann::json& arguments);

            ToolOptions m_options;
            Connection m_connection;
            ScreenshotView m_view;
        };

        // A pause between input events where timing matters (double clicks, drags).
        constexpr int kInputPauseMs = 15;

        // Instructions sent to the AI when it starts, about how the tools fit together.
        std::string ServerInstructions();
    } // namespace Mcp
} // namespace Marconi
