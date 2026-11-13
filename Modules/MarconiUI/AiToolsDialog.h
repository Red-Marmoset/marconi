// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The AI tools window: which AI tools are on this computer, whether each can use Marconi, and Add/Remove for each

#pragma once

#include <wx/dialog.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class wxBoxSizer;
class wxButton;
class wxStaticText;

namespace MarconiUI
{
    class AiToolsDialog : public wxDialog
    {
    public:
        explicit AiToolsDialog(wxWindow* parent);
        ~AiToolsDialog() override;

    private:
        // One tool as `MarconiMcp --status` describes it.
        struct ToolRow
        {
            std::string id;
            std::string name;
            bool found = false;
            bool known = false;      // whether the tool could be asked about Marconi (not for Claude Desktop)
            bool registered = false;
            bool current = false;    // registered, and pointing at this Marconi's MarconiMcp
            wxStaticText* state = nullptr;
            wxButton* add = nullptr;
            wxButton* remove = nullptr;
        };

        // Runs MarconiMcp with `arguments` on a worker thread; `done` gets its exit code and output on the UI thread.
        void RunInBackground(std::vector<std::string> arguments, std::function<void(int, const std::string&)> done);
        void CheckTools();
        void ShowStatus(const std::string& output);
        void Change(size_t row, bool add);
        void SetBusy(bool busy, const wxString& message);

        std::string m_mcp; // the MarconiMcp beside this program
        std::vector<ToolRow> m_rows;
        wxStaticText* m_message = nullptr;
        wxString m_afterRefresh; // what the last Add or Remove said, shown once the list has been checked again
        std::thread m_worker;
        std::atomic<bool> m_busy{false};
    };

    // The MarconiMcp program that ships beside Marconi, or empty if it isn't there.
    std::string FindMarconiMcp();
} // namespace MarconiUI
