// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for rmvnc

#include "RmVncApp.h"

#include "ConnectFrame.h"
#include "ViewerFrame.h"

#include <RmVncCore/Address.h>
#include <RmVncCore/Version.h>

#include <Log/Log.h>

#include <wx/config.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

namespace RmVncUI
{
    bool RmVncApp::OnInit()
    {
        // wxApp::OnInit's command line parser would reject our positional address argument.
        SetAppName("rmvnc");
        SetVendorName("Red Marmoset Studios");

        // wxRegConfig on Windows, wxFileConfig (XDG / ~/Library/Preferences) elsewhere.
        wxConfigBase::Set(wxConfigBase::Create());

        // Logs go to <user local data>/logs (e.g. %LOCALAPPDATA%/rmvnc/logs, ~/Library/Application Support/rmvnc/logs).
        wxString logDir = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs";
        wxFileName::Mkdir(logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        Log::InitializeFileLogging(logDir.utf8_string(), "rmvnc");
        LOG_INFO("App", "rmvnc %s starting", RmVnc::GetVersionString());

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

    void RmVncApp::OnUnhandledException()
    {
        LOG_WARNING("App", "Unhandled exception in event handler");
        Log::FlushLogFile();
        wxApp::OnUnhandledException();
    }

    int RmVncApp::OnExit()
    {
        LOG_INFO("App", "Exiting");
        Log::ShutdownFileLogging();
        delete wxConfigBase::Set(nullptr);
        return wxApp::OnExit();
    }
} // namespace RmVncUI
