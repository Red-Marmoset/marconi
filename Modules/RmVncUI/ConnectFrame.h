// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Start window: type an address and connect

#pragma once

#include <wx/frame.h>

class wxTextCtrl;

namespace RmVncUI
{
    class ConnectFrame : public wxFrame
    {
    public:
        ConnectFrame();

    private:
        void OnConnect(wxCommandEvent& event);

        wxTextCtrl* m_address = nullptr;
    };
} // namespace RmVncUI
