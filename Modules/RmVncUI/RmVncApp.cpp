// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for rmvnc

#include "RmVncApp.h"

#include "ConnectFrame.h"
#include "ViewerFrame.h"

#include <RmVncCore/Address.h>

#include <wx/config.h>
#include <wx/msgdlg.h>

namespace RmVncUI
{
    bool RmVncApp::OnInit()
    {
        // wxApp::OnInit's command line parser would reject our positional address argument.
        SetAppName("rmvnc");
        SetVendorName("Red Marmoset Studios");

        // wxRegConfig on Windows, wxFileConfig (XDG / ~/Library/Preferences) elsewhere.
        wxConfigBase::Set(wxConfigBase::Create());

        // "rmvnc host[:display]" connects straight away.
        if (argc > 1)
        {
            RmVnc::Address address;
            std::string error;
            if (RmVnc::ParseAddress(argv[1].utf8_string(), address, error))
            {
                ViewerOptions options;
                options.address = address;
                (new ViewerFrame(options))->Show();
                return true;
            }
            wxMessageBox(wxString::FromUTF8(error), "rmvnc", wxOK | wxICON_WARNING);
        }

        ConnectFrame* frame = new ConnectFrame();
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
