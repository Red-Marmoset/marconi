// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for rmvnc

#pragma once

#include <wx/wx.h>

namespace RmVncUI
{
    class RmVncApp : public wxApp
    {
    public:
        bool OnInit() override;
        int OnExit() override;
        void OnUnhandledException() override;
    };
} // namespace RmVncUI

wxDECLARE_APP(RmVncUI::RmVncApp);
