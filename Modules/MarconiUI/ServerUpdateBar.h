// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The bar across the top of the viewer that offers to update an older Marconi Server and says how the update goes

#pragma once

#include <wx/panel.h>

#include <functional>
#include <utility>
#include <vector>

class wxButton;
class wxStaticText;

namespace MarconiUI
{
    class ServerUpdateBar : public wxPanel
    {
    public:
        using Button = std::pair<wxString, std::function<void()>>;

        explicit ServerUpdateBar(wxWindow* parent);

        // What the bar says, and up to three buttons (none for a message that is only progress).
        void SetContent(const wxString& message, const std::vector<Button>& buttons);

    private:
        wxStaticText* m_message = nullptr;
        wxButton* m_buttons[3] = {nullptr, nullptr, nullptr};
        std::function<void()> m_actions[3];
    };
} // namespace MarconiUI
