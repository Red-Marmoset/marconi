// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for Marconi

#pragma once

#include <wx/wx.h>

namespace MarconiUI
{
    class MarconiApp : public wxApp
    {
    public:
        bool OnInit() override;
        int OnExit() override;
        void OnUnhandledException() override;
    };
} // namespace MarconiUI

wxDECLARE_APP(MarconiUI::MarconiApp);
