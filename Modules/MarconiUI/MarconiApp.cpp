// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for Marconi

#include "MarconiApp.h"

#include "ConnectionStore.h"
#include "ConnectionsFrame.h"
#include "ViewerFrame.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

namespace MarconiUI
{
    bool MarconiApp::OnInit()
    {
        // wxApp::OnInit's command line parser would reject our positional address argument.
        SetAppName("marconi");
        SetAppDisplayName("Marconi");
        SetVendorName("Red Marmoset Studios");

#if defined(__WXGTK__)
        // ~/.config/marconi and ~/.local/share/marconi rather than wx's traditional ~/.marconi.
        wxStandardPaths::Get().SetFileLayout(wxStandardPaths::FileLayout_XDG);
#endif
        // wxRegConfig on Windows, wxFileConfig (XDG / ~/Library/Preferences) elsewhere.
        wxConfigBase::Set(wxConfigBase::Create());

        // Logs go to <user local data>/logs (e.g. %LOCALAPPDATA%/marconi/logs, ~/Library/Application Support/marconi/logs).
        wxString logDir = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs";
        wxFileName::Mkdir(logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        Log::InitializeFileLogging(logDir.utf8_string(), "marconi");
        LOG_INFO("App", "Marconi %s starting", Marconi::GetVersionString());
        ConnectionStore::MigrateFromRmvnc();

        // "marconi [options] host[:display]" connects straight away.
        static const wxCmdLineEntryDesc kCommandLine[] = {
            {wxCMD_LINE_SWITCH, "h", "help", "show this help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP},
            {wxCMD_LINE_OPTION, nullptr, "scale", "fit (default) or 100", wxCMD_LINE_VAL_STRING, 0},
            {wxCMD_LINE_SWITCH, nullptr, "view-only", "don't send keyboard or mouse input", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "fullscreen", "start full screen", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "no-clipboard", "don't share the clipboard", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_PARAM, nullptr, nullptr, "host[:display] | host::port", wxCMD_LINE_VAL_STRING,
             wxCMD_LINE_PARAM_OPTIONAL},
            wxCMD_LINE_DESC_END,
        };
        wxCmdLineParser parser(kCommandLine, argc, argv);
        if (parser.Parse() != 0)
        {
            return false; // help shown or bad arguments (wx reports them)
        }
        if (parser.GetParamCount() > 0)
        {
            ViewerOptions options;
            std::string error;
            wxString scale;
            if (parser.Found("scale", &scale) && scale != "fit" && scale != "100")
            {
                error = "--scale must be 'fit' or '100'";
            }
            else if (Marconi::ParseAddress(parser.GetParam(0).utf8_string(), options.address, error))
            {
                options.scaleMode = scale == "100" ? ScaleMode::Actual : ScaleMode::Fit;
                options.viewOnly = parser.Found("view-only");
                options.fullScreen = parser.Found("fullscreen");
                options.clipboard = !parser.Found("no-clipboard");
                (new ViewerFrame(options))->Show();
                return true;
            }
            wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING);
        }

        ConnectionsFrame* frame = new ConnectionsFrame();
        frame->Show(true);
        SetTopWindow(frame);
        return true;
    }

    void MarconiApp::OnUnhandledException()
    {
        LOG_WARNING("App", "Unhandled exception in event handler");
        Log::FlushLogFile();
        wxApp::OnUnhandledException();
    }

    int MarconiApp::OnExit()
    {
        LOG_INFO("App", "Exiting");
        Log::ShutdownFileLogging();
        delete wxConfigBase::Set(nullptr);
        return wxApp::OnExit();
    }
} // namespace MarconiUI
