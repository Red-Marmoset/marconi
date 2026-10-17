// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The bar shown across the top of the viewer while a lost connection is being retried

#pragma once

#include <wx/panel.h>

#include <functional>

class wxButton;
class wxStaticText;

namespace MarconiUI
{
    class ConnectionLostBar : public wxPanel
    {
    public:
        ConnectionLostBar(wxWindow* parent, std::function<void()> onRetry, std::function<void()> onDisconnect);

        // What the bar says, e.g. "Connection lost (the server closed the connection). Retrying in 4 s".
        void SetMessage(const wxString& message);
        // Whether "Retry Now" can be pressed (not while an attempt is under way).
        void EnableRetry(bool enable);

    private:
        wxStaticText* m_message = nullptr;
        wxButton* m_retry = nullptr;
        wxButton* m_disconnect = nullptr;
    };
} // namespace MarconiUI
