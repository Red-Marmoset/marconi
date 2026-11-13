// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tools an AI tool can call: connecting, screenshots, and (later) the mouse, keyboard and clipboard

#pragma once

#include <MarconiMcp/Connection.h>
#include <MarconiMcp/Protocol.h>

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
            ToolResult Disconnect();
            ToolResult Status();
            ToolResult Screenshot(const nlohmann::json& arguments);

            ToolOptions m_options;
            Connection m_connection;
            ScreenshotView m_view;
        };

        // Instructions sent to the AI when it starts, about how the tools fit together.
        std::string ServerInstructions();
    } // namespace Mcp
} // namespace Marconi
