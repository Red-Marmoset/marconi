// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The AI tools window: which AI tools are on this computer, whether each can use Marconi, and Add/Remove for each.
// Everything is done by the MarconiMcp beside Marconi (--status, --register, --unregister), on a worker thread.

#include "AiToolsDialog.h"

#include "Appearance.h"
#include "Controls.h"

#include <Process/Process.h>

#include <Log/Log.h>

#include <json/single_include/nlohmann/json.hpp>

#include <MarconiConnections/ConnectionStore.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <filesystem>
#include <sstream>

namespace MarconiUI
{
    namespace
    {
        const char* const kTools[][2] = {
            {"claude-code", "Claude Code"},
            {"codex", "Codex"},
            {"claude-desktop", "Claude Desktop"},
        };
    } // namespace

    std::string FindMarconiMcp()
    {
        const std::string folder = Process::ExecutableDirectory();
        std::error_code error;
#if defined(_WIN32)
        const std::vector<std::string> names = {"MarconiMcp.exe"};
        const char separator = '\\';
#else
        // The Linux package installs it as /usr/bin/marconi-mcp beside /usr/bin/marconi.
        const std::vector<std::string> names = {"MarconiMcp", "marconi-mcp"};
        const char separator = '/';
#endif
        for (const std::string& name : names)
        {
            const std::string path = folder + separator + name;
            if (std::filesystem::exists(path, error))
            {
                return path;
            }
        }
        return std::string();
    }

    AiToolsDialog::AiToolsDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "AI tools", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
          m_mcp(FindMarconiMcp())
    {
        const int space = FromDIP(20);
        wxBoxSizer* layout = new wxBoxSizer(wxVERTICAL);
        wxStaticText* heading = new wxStaticText(this, wxID_ANY, "Connect Marconi to your AI tools");
        heading->SetFont(HeadingFont(heading->GetFont(), 1.3f));
        layout->Add(heading, 0, wxALL, space);
        wxStaticText* intro = new wxStaticText(
            this, wxID_ANY,
            "AI tools you add here can use Marconi to connect to the computers you name, see their screens, and use "
            "their mouse, keyboard and clipboard. They still ask before each step unless you tell them not to.");
        intro->Wrap(FromDIP(440));
        layout->Add(intro, 0, wxLEFT | wxRIGHT | wxBOTTOM, space);

        wxFlexGridSizer* grid = new wxFlexGridSizer(4, FromDIP(8), FromDIP(16));
        grid->AddGrowableCol(1);
        for (const auto& tool : kTools)
        {
            ToolRow row;
            row.id = tool[0];
            row.name = tool[1];
            wxStaticText* name = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(row.name));
            name->SetFont(HeadingFont(name->GetFont()));
            row.state = new wxStaticText(this, wxID_ANY, "Checking...");
            row.add = new wxButton(this, wxID_ANY, "Add");
            row.remove = new wxButton(this, wxID_ANY, "Remove");
            row.add->Disable();
            row.remove->Disable();
            grid->Add(name, 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(row.state, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);
            grid->Add(row.add, 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(row.remove, 0, wxALIGN_CENTER_VERTICAL);
            const size_t index = m_rows.size();
            row.add->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent&) { Change(index, true); });
            row.remove->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent&) { Change(index, false); });
            m_rows.push_back(row);
        }
        layout->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, space);

        // Saved connections are offered to the AI tools by name; their passwords are used without being shown.
        wxCheckBox* passwords = new wxCheckBox(this, wxID_ANY, "Let AI tools use the passwords saved in Marconi");
        passwords->SetValue(ConnectionStore::AiToolsMayUsePasswords());
        passwords->SetToolTip("They connect to your saved computers without being told the password; it never "
                              "appears in the conversation. To keep one computer away from them, tick \"Hide from AI "
                              "tools\" in its connection settings.");
        passwords->Bind(wxEVT_CHECKBOX, [passwords](wxCommandEvent&) {
            ConnectionStore::SetAiToolsMayUsePasswords(passwords->GetValue());
        });
        layout->Add(passwords, 0, wxLEFT | wxRIGHT | wxBOTTOM, space);

        m_message = new wxStaticText(this, wxID_ANY, "");
        layout->Add(m_message, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, space);
        layout->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, space);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
        SetSizerAndFit(layout);
        SetMinSize(wxSize(FromDIP(520), -1));
        Fit();
        StyleWindow(this);
        CentreOnParent();

        if (m_mcp.empty())
        {
            for (ToolRow& row : m_rows)
            {
                row.state->SetLabel("");
            }
            m_message->SetLabel("MarconiMcp, the part of Marconi AI tools use, isn't beside this program.");
            return;
        }
        CheckTools();
    }

    AiToolsDialog::~AiToolsDialog()
    {
        // MarconiMcp's commands finish in a minute at most (they stop the AI tool's command if it takes longer).
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void AiToolsDialog::RunInBackground(std::vector<std::string> arguments,
                                        std::function<void(int, const std::string&)> done)
    {
        if (m_worker.joinable())
        {
            m_worker.join();
        }
        arguments.insert(arguments.begin(), m_mcp);
        m_worker = std::thread([this, arguments, done]() {
            const Process::Result result = Process::Run(arguments, 120000);
            const int code = result.started && !result.timedOut ? result.exitCode : -1;
            const std::string output = result.started ? result.output : result.error;
            CallAfter([done, code, output]() { done(code, output); });
        });
    }

    void AiToolsDialog::SetBusy(bool busy, const wxString& message)
    {
        m_busy = busy;
        m_message->SetLabel(message);
        m_message->Wrap(FromDIP(480));
        for (ToolRow& row : m_rows)
        {
            row.add->Enable(!busy && row.found && !row.current);
            row.remove->Enable(!busy && row.found && row.known && row.registered);
        }
        Layout();
        Fit();
    }

    void AiToolsDialog::CheckTools()
    {
        SetBusy(true, "Looking for AI tools...");
        RunInBackground({"--status", "--quiet"}, [this](int code, const std::string& output) {
            if (code != 0)
            {
                SetBusy(false, "Couldn't look for AI tools: " + wxString::FromUTF8(output));
                return;
            }
            ShowStatus(output);
            SetBusy(false, m_afterRefresh);
            m_afterRefresh.clear();
        });
    }

    void AiToolsDialog::ShowStatus(const std::string& output)
    {
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line))
        {
            const nlohmann::json status = nlohmann::json::parse(line, nullptr, false);
            if (!status.is_object())
            {
                continue;
            }
            for (ToolRow& row : m_rows)
            {
                if (status.value("tool", std::string()) != row.id)
                {
                    continue;
                }
                row.found = status.value("found", false);
                row.known = status.contains("registered");
                row.registered = status.value("registered", false);
                row.current = status.value("current", false);
                wxString state;
                if (!row.found)
                {
                    state = "Not on this computer";
                }
                else if (!row.known)
                {
                    state = row.id == "claude-desktop" ? "Add opens Claude to confirm; remove it in Claude's settings"
                                                       : "Couldn't ask it about Marconi";
                }
                else if (row.current)
                {
                    state = "Can use Marconi";
                }
                else if (row.registered)
                {
                    state = "Uses another copy of Marconi; Add points it at this one";
                }
                else
                {
                    state = "Can't use Marconi yet";
                }
                row.state->SetLabel(state);
            }
        }
    }

    void AiToolsDialog::Change(size_t index, bool add)
    {
        if (m_busy || index >= m_rows.size())
        {
            return;
        }
        const ToolRow& row = m_rows[index];
        SetBusy(true, wxString(add ? "Adding Marconi to " : "Removing Marconi from ") + wxString::FromUTF8(row.name) +
                          "...");
        RunInBackground({add ? "--register" : "--unregister", row.id, "--quiet"},
                        [this](int code, const std::string& output) {
            const wxString message = wxString::FromUTF8(output).Trim();
            LOG_INFO("App", "AI tools: %s", output.c_str());
            m_afterRefresh = code == 0 ? message : "That didn't work: " + message;
            CheckTools();
        });
    }
} // namespace MarconiUI
