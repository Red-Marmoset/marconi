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
#if defined(__WXOSX__)
        // A vnc:// link opened in another program (Marconi.app registers for them in its Info.plist).
        void MacOpenURL(const wxString& url) override;
#endif
    };
} // namespace MarconiUI

wxDECLARE_APP(MarconiUI::MarconiApp);
