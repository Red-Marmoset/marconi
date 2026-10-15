// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for rmvnc

#include "RmVncApp.h"

#include <RmVncCore/Version.h>

#include <wx/config.h>

namespace RmVncUI
{
    bool RmVncApp::OnInit()
    {
        if (!wxApp::OnInit())
        {
            return false;
        }

        SetAppName("rmvnc");
        SetVendorName("Red Marmoset Studios");

        // wxRegConfig on Windows, wxFileConfig (XDG / ~/Library/Preferences) elsewhere.
        wxConfigBase::Set(wxConfigBase::Create());

        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, wxString::Format("rmvnc %s", RmVnc::GetVersionString()),
                                     wxDefaultPosition, wxSize(800, 600));
        frame->Show(true);
        SetTopWindow(frame);
        return true;
    }

    int RmVncApp::OnExit()
    {
        delete wxConfigBase::Set(nullptr);
        return wxApp::OnExit();
    }
} // namespace RmVncUI
