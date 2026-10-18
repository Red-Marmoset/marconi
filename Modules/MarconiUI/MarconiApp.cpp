// Copyright (c) 2026 Red Marmoset Studios Ltd.
// wxApp for Marconi

#include "MarconiApp.h"

#include "Appearance.h"
#include "ConnectionStore.h"
#include "ConnectionsFrame.h"
#include "UserData.h"
#include "ViewerFrame.h"

#include <MarconiCore/Zoom.h>
#include <MarconiCore/Address.h>
#include <MarconiCore/Files/SftpFiles.h>
#include <MarconiCore/Net/Discovery.h>
#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <cstdlib>

#include <wx/arrstr.h>
#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/font.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#if !defined(__WINDOWS__)
#include <sys/stat.h>
#endif

namespace MarconiUI
{
    namespace
    {
        // Proves file transfer end to end without a window: everything it creates on the remote computer is removed.
        void RunSftpCheck(const ViewerOptions& options)
        {
            Marconi::Files::SftpFiles remote;
            std::string error;
            std::string host = options.address.host;
            if (!remote.Connect(host, 22, 10000, error))
            {
                LOG_WARNING("App", "SFTP check: couldn't connect to %s: %s", host.c_str(), error.c_str());
                return;
            }
            LOG_INFO("App", "SFTP check: %s host key %s %s", host.c_str(), remote.GetHostKey().algorithm.c_str(),
                     remote.GetHostKey().fingerprint.c_str());
            if (!remote.Login(options.credentials.username, options.credentials.password, error))
            {
                LOG_WARNING("App", "SFTP check: login as '%s' failed: %s", options.credentials.username.c_str(),
                            error.c_str());
                return;
            }
            std::vector<Marconi::Files::FileEntry> entries;
            if (!remote.List(remote.GetHome(), entries, error))
            {
                LOG_WARNING("App", "SFTP check: couldn't list %s: %s", remote.GetHome().c_str(), error.c_str());
                return;
            }
            LOG_INFO("App", "SFTP check: home is %s with %zu items", remote.GetHome().c_str(), entries.size());

            const std::string folder = Marconi::Files::JoinPath(remote.GetHome(), "Marconi SFTP check");
            const std::string file = Marconi::Files::JoinPath(folder, "check.txt");
            const std::string content = "Marconi SFTP check " + std::to_string(std::rand());
            bool ok = remote.MakeFolder(folder, error);
            std::unique_ptr<Marconi::Files::FileWriter> writer;
            if (ok)
            {
                writer = remote.OpenWrite(file, false, error);
                ok = writer && writer->Write(content.data(), content.size(), error) && writer->Finish(error);
                writer.reset();
            }
            std::string readBack;
            if (ok)
            {
                std::unique_ptr<Marconi::Files::FileReader> reader = remote.OpenRead(file, error);
                char buffer[256];
                ptrdiff_t got = 0;
                while (reader && (got = reader->Read(buffer, sizeof(buffer), error)) > 0)
                {
                    readBack.append(buffer, static_cast<size_t>(got));
                }
                ok = reader && got == 0;
            }
            std::string ignored;
            remote.Remove(file, ignored);
            remote.RemoveFolder(folder, ignored);
            Marconi::Files::FileEntry leftover;
            bool tidy = !remote.Stat(folder, leftover, ignored);
            if (ok && readBack == content && tidy)
            {
                LOG_INFO("App", "SFTP check: passed (wrote, read back and removed %zu bytes)", content.size());
            }
            else
            {
                LOG_WARNING("App", "SFTP check: failed: %s (read back %zu bytes, tidied up: %s)", error.c_str(),
                            readBack.size(), tidy ? "yes" : "no");
            }
        }
    } // namespace

    bool MarconiApp::OnInit()
    {
#if !defined(__WINDOWS__)
        // Settings, logs and anything else Marconi writes are private to this user.
        umask(S_IRWXG | S_IRWXO);
#endif
        // wxApp::OnInit's command line parser would reject our positional address argument.
        SetAppName("marconi");
        SetAppDisplayName("Marconi");
        SetVendorName("Red Marmoset Studios");

#if defined(__WXGTK__)
        // ~/.config/marconi.conf and ~/.local/share/marconi rather than wx's traditional ~/.marconi.
        wxStandardPaths::Get().SetFileLayout(wxStandardPaths::FileLayout_XDG);
#endif
        // wxRegConfig on Windows, wxFileConfig (XDG / ~/Library/Preferences) elsewhere.
        wxConfigBase::Set(wxConfigBase::Create());
#if !defined(__WINDOWS__)
        // Earlier versions created the settings file with the usual permissions; make it private now.
        wxFileName settingsFile =
            wxFileConfig::GetLocalFile(GetAppName(), wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_GLOBAL_FILE);
        if (settingsFile.FileExists())
        {
            chmod(settingsFile.GetFullPath().utf8_string().c_str(), S_IRUSR | S_IWUSR);
        }
#endif
        InitializeAppearance(); // Native wx appearance must be selected before creating any window.
        wxImage::AddHandler(new wxPNGHandler); // diagnostic snapshots

        // "marconi [options] host[:display]" connects straight away.
        static const wxCmdLineEntryDesc kCommandLine[] = {
            {wxCMD_LINE_SWITCH, "h", "help", "show this help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP},
            {wxCMD_LINE_OPTION, nullptr, "scale", "fit (default) or a zoom percentage: 50, 75, 100, 125, 150 or 200",
             wxCMD_LINE_VAL_STRING, 0},
            {wxCMD_LINE_SWITCH, nullptr, "view-only", "don't send keyboard or mouse input", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "fullscreen", "start full screen", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "no-clipboard", "don't share the clipboard", wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_SWITCH, nullptr, "ssh",
             "connect through the computer's SSH server (Remote Login), logging in with the same account",
             wxCMD_LINE_VAL_NONE, 0},
            {wxCMD_LINE_OPTION, nullptr, "saved", "open a saved connection (by name, address or id)",
             wxCMD_LINE_VAL_STRING, 0},
            {wxCMD_LINE_SWITCH, nullptr, "remove-my-data",
             "remove this user's saved connections, passwords, settings and logs, then exit", wxCMD_LINE_VAL_NONE, 0},
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
            {wxCMD_LINE_SWITCH, nullptr, "discover", "diagnostics: log the machines found on the network, then exit",
             wxCMD_LINE_VAL_NONE, wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_SWITCH, nullptr, "files", "open the file transfer panel straight away", wxCMD_LINE_VAL_NONE,
             wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_SWITCH, nullptr, "files-check",
             "headless diagnostics: copy a small file to a TightVNC server and back over the VNC connection, tidy up",
             wxCMD_LINE_VAL_NONE, wxCMD_LINE_HIDDEN},
            {wxCMD_LINE_SWITCH, nullptr, "sftp-check",
             "diagnostics: log in over SFTP with the connection's account, copy a small file there and back, tidy up",
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
        if (parser.Found("remove-my-data"))
        {
            // Before any log file is open, so the whole log folder can go now.
            RemoveAllUserData();
            RemoveLogDirectory();
            return false;
        }

        // Logs go to <user local data>/logs (e.g. %LOCALAPPDATA%/marconi/logs, ~/Library/Application Support/marconi/logs).
        wxString logDir = GetLogDirectory();
        wxFileName::Mkdir(logDir, wxS_IRUSR | wxS_IWUSR | wxS_IXUSR, wxPATH_MKDIR_FULL);
        Log::InitializeFileLogging(logDir.utf8_string(), "marconi");
        LOG_INFO("App", "Marconi %s (%s) starting", Marconi::GetVersionString(), Marconi::GetVersionCommit());
#if wxUSE_PRIVATE_FONTS
        // Load before creating the wordmark or enumerating font faces. The font belongs to this process only.
        // Windows and development builds keep the fonts beside the executable; the macOS bundle keeps them in
        // Contents/Resources.
        for (const auto* fontFile : {"NebulaSans-Black.ttf", "NebulaSans-Semibold.ttf"})
        {
            wxFileName brandFont(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(), fontFile);
            if (!brandFont.FileExists())
            {
                brandFont.AssignDir(wxStandardPaths::Get().GetResourcesDir());
                brandFont.SetFullName(fontFile);
            }
            bool fontLoaded = false;
            if (brandFont.FileExists())
            {
                wxLogNull suppressFontDialog; // Missing/corrupt optional branding must not interrupt startup.
                fontLoaded = wxFont::AddPrivateFont(brandFont.GetFullPath());
            }
            if (!fontLoaded)
                LOG_INFO("App", "Bundled font %s unavailable; using installed or system font", fontFile);
        }
#endif
        ConnectionStore::MigrateFromRmvnc();

        if (parser.Found("discover"))
        {
            std::vector<Marconi::Net::DiscoveredService> services = Marconi::Net::Discover(3000);
            for (const Marconi::Net::DiscoveredService& service : services)
            {
                LOG_INFO("App", "Found %s: %s %s:%u (%s)", Marconi::ProtocolName(service.protocol),
                         service.instance.c_str(), service.hostName.c_str(), service.port, service.ipv4.c_str());
            }
            LOG_INFO("App", "Found %zu service%s on the network", services.size(), services.size() == 1 ? "" : "s");
            return false;
        }
        const bool headless = parser.Found("headless");
        if (headless || parser.Found("sftp-check") || parser.Found("files"))
        {
            // No window and nobody watching: a failed wxWidgets check goes to the log instead of an alert.
            wxSetAssertHandler([](const wxString& file, int line, const wxString& function, const wxString& condition,
                                  const wxString& message) {
                LOG_WARNING("App", "wxWidgets check failed at %s:%d in %s: %s %s", file.utf8_string().c_str(), line,
                            function.utf8_string().c_str(), condition.utf8_string().c_str(),
                            message.utf8_string().c_str());
            });
        }
        wxString savedName;
        const bool fromSaved = parser.Found("saved", &savedName);
        if (parser.GetParamCount() > 0 || fromSaved)
        {
            ViewerOptions options;
            std::string error;
            wxString scale;
            bool ok = false;
            long zoomPercent = 0;
            if (parser.Found("scale", &scale) && scale != "fit" &&
                !(scale.ToLong(&zoomPercent) && Marconi::Zoom::IsLevel(static_cast<int>(zoomPercent))))
            {
                error = "--scale must be 'fit' or one of 50, 75, 100, 125, 150, 200";
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
                    options.scaleMode = scale == "fit" ? ScaleMode::Fit : ScaleMode::Fixed;
                    if (zoomPercent > 0)
                    {
                        options.zoomPercent = static_cast<int>(zoomPercent);
                    }
                }
                options.viewOnly = options.viewOnly || parser.Found("view-only");
                options.fullScreen = parser.Found("fullscreen");
                options.showFiles = parser.Found("files");
                options.clipboard = options.clipboard && !parser.Found("no-clipboard");
                options.sshTunnel = options.sshTunnel || parser.Found("ssh");
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
                    options.filesCheck = parser.Found("files-check");
                }
                if (parser.Found("sftp-check"))
                {
                    RunSftpCheck(options);
                    return false;
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
        if (ShouldRemoveLogsOnExit())
        {
            RemoveLogDirectory(); // the log file just closed was the last thing in the way
        }
        delete wxConfigBase::Set(nullptr);
        return wxApp::OnExit();
    }
} // namespace MarconiUI

#if defined(__WXOSX__)
namespace MarconiUI
{
    void MarconiApp::MacOpenURL(const wxString& url)
    {
        ViewerOptions options;
        std::string error;
        ConnectionStore store;
        SavedConnection saved;
        if (store.FindByAddress(url, saved) && ViewerOptionsFromSaved(saved, store, options, error))
        {
            store.MarkUsed(saved.id);
        }
        else if (!Marconi::ParseAddress(url.utf8_string(), options.address, error))
        {
            wxMessageBox(wxString::FromUTF8(error), "Marconi", wxOK | wxICON_WARNING);
            return;
        }
        (new ViewerFrame(options))->Show();
    }
} // namespace MarconiUI
#endif
