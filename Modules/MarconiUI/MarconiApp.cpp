// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for Marconi

#include "MarconiApp.h"

#include "Appearance.h"
#include "ConnectionStore.h"
#include "ConnectionsFrame.h"
#include "ViewerFrame.h"

#include <MarconiCore/Address.h>
#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <wx/arrstr.h>
#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/font.h>
#include <wx/log.h>
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
        InitializeAppearance(); // Native wx appearance must be selected before creating any window.
        wxImage::AddHandler(new wxPNGHandler); // diagnostic snapshots

        // Logs go to <user local data>/logs (e.g. %LOCALAPPDATA%/marconi/logs, ~/Library/Application Support/marconi/logs).
        wxString logDir = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs";
        wxFileName::Mkdir(logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        Log::InitializeFileLogging(logDir.utf8_string(), "marconi");
        LOG_INFO("App", "Marconi %s (%s) starting", Marconi::GetVersionString(), Marconi::GetVersionCommit());
#if wxUSE_PRIVATE_FONTS
        // Load before creating the wordmark or enumerating font faces. The font belongs to this process only.
        const wxFileName brandFont(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(),
                                   "NebulaSans-Bold.ttf");
        bool fontLoaded = false;
        if (brandFont.FileExists())
        {
            wxLogNull suppressFontDialog; // Missing/corrupt optional branding must not interrupt startup.
            fontLoaded = wxFont::AddPrivateFont(brandFont.GetFullPath());
        }
        if (!fontLoaded)
            LOG_INFO("App", "Bundled Nebula Sans unavailable; using installed or system font");
#endif
        ConnectionStore::MigrateFromRmvnc();

        // "marconi [options] host[:display]" connects straight away.
        static const wxCmdLineEntryDesc kCommandLine[] = {
            {wxCMD_LINE_SWITCH, "h", "help", "show this help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP},
            {wxCMD_LINE_OPTION, nullptr, "scale", "fit (default) or 100", wxCMD_LINE_VAL_STRING, 0},
            {wxCMD_LINE_SWITCH, nullptr, "view-only", "don't send keyboard or mouse input", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "fullscreen", "start full screen", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "no-clipboard", "don't share the clipboard", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_OPTION, nullptr, "saved", "open a saved connection (by name, address or id)",
             wxCMD_LINE_VAL_STRING, 0},
            {wxCMD_LINE_SWITCH, nullptr, "headless",
             "no window or dialogs: log and save snapshots to the log folder (testing, automation)",
             wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_OPTION, nullptr, "snapshot-every", "headless: seconds between snapshots (default 5)",
             wxCMD_LINE_VAL_NUMBER, 0},
            {wxCMD_LINE_OPTION, nullptr, "quit-after", "headless: seconds before closing (default 60)",
             wxCMD_LINE_VAL_NUMBER, 0},
            {wxCMD_LINE_OPTION, nullptr, "encodings",
             "headless diagnostics: comma-separated encodings to send exactly (e.g. 0,-223)", wxCMD_LINE_VAL_STRING,
             wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_SWITCH, nullptr, "no-pixel-format-resend",
             "headless diagnostics: don't repeat SetPixelFormat after a resize", wxCMD_LINE_VAL_NONE,
             wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_SWITCH, nullptr, "no-recovery",
             "headless diagnostics: never re-request the desktop or reconnect when no picture arrives",
             wxCMD_LINE_VAL_NONE, wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_PARAM, nullptr, nullptr, "host[:display] | host::port", wxCMD_LINE_VAL_STRING,
             wxCMD_LINE_PARAM_OPTIONAL},
            wxCMD_LINE_DESC_END,
        };
        wxCmdLineParser parser(kCommandLine, argc, argv);
        if (parser.Parse() != 0)
        {
            return false; // help shown or bad arguments (wx reports them)
        }
        const bool headless = parser.Found("headless");
        wxString savedName;
        const bool fromSaved = parser.Found("saved", &savedName);
        if (parser.GetParamCount() > 0 || fromSaved)
        {
            ViewerOptions options;
            std::string error;
            wxString scale;
            bool ok = false;
            if (parser.Found("scale", &scale) && scale != "fit" && scale != "100")
            {
                error = "--scale must be 'fit' or '100'";
            }
            else if (fromSaved)
            {
                ConnectionStore store;
                ok = false;
                error = "No saved connection called '" + savedName.utf8_string() + "'";
                for (const SavedConnection& connection : store.LoadAll())
                {
                    if (connection.id == savedName || connection.name.CmpNoCase(savedName) == 0 ||
                        connection.address.CmpNoCase(savedName) == 0)
                    {
                        ok = ViewerOptionsFromSaved(connection, store, options, error);
                        break;
                    }
                }
            }
            else
            {
                ok = Marconi::ParseAddress(parser.GetParam(0).utf8_string(), options.address, error);
            }
            if (ok)
            {
                // Command-line switches override a saved connection's settings.
                if (!scale.empty())
                {
                    options.scaleMode = scale == "100" ? ScaleMode::Actual : ScaleMode::Fit;
                }
                options.viewOnly = options.viewOnly || parser.Found("view-only");
                options.fullScreen = parser.Found("fullscreen");
                options.clipboard = options.clipboard && !parser.Found("no-clipboard");
                if (headless)
                {
                    long seconds = 0;
                    options.headless = true;
                    options.clipboard = false; // never touch the user's clipboard from a headless run
                    options.fullScreen = false;
                    if (parser.Found("snapshot-every", &seconds))
                    {
                        options.snapshotEverySeconds = static_cast<int>(seconds);
                    }
                    if (parser.Found("quit-after", &seconds))
                    {
                        options.quitAfterSeconds = static_cast<int>(seconds);
                    }
                    wxString encodings;
                    if (parser.Found("encodings", &encodings))
                    {
                        for (wxString token : wxSplit(encodings, ','))
                        {
                            long encoding = 0;
                            if (token.Trim().Trim(false).ToLong(&encoding))
                            {
                                options.encodings.push_back(static_cast<int32_t>(encoding));
                            }
                        }
                    }
                    options.resendPixelFormatOnResize = !parser.Found("no-pixel-format-resend");
                    options.recovery = !parser.Found("no-recovery");
                }
                ViewerFrame* viewer = new ViewerFrame(options);
                if (!headless)
                {
                    viewer->Show();
                }
                return true;
            }
            if (headless)
            {
                LOG_WARNING("App", "Headless: %s", error.c_str());
                return false;
            }
            wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING);
        }
        else if (headless)
        {
            LOG_WARNING("App", "Headless needs an address or --saved");
            return false;
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
