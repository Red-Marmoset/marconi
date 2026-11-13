// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiMcp's view of the connections saved in Marconi, read through the app's own ConnectionStore

#pragma once

#include <MarconiMcp/SavedComputers.h>

namespace MarconiMcpApp
{
    // Skips connections marked "Hide from AI tools"; offers saved passwords only while Settings > AI tools allows
    // it. Reads the store afresh each time, so connections saved in Marconi meanwhile are there. Needs wx initialised
    // and MarconiUI::UseMarconiSettings() called first.
    class StoreComputers : public Marconi::Mcp::SavedComputers
    {
    public:
        std::vector<Marconi::Mcp::SavedComputer> List() override;
        bool Password(const std::string& id, std::string& out) override;
    };
} // namespace MarconiMcpApp
