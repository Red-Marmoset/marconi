// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The windows for putting Marconi Server on a computer

#include "ServerInstall.h"

#include <MarconiCore/ServerInstaller.h>
#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace MarconiUI
{
    std::string FindBundledServerInstaller()
    {
        wxFileName program(wxStandardPaths::Get().GetExecutablePath());
        return Marconi::ServerInstaller::Find(program.GetPath().utf8_string(), Marconi::GetVersionString());
    }

    namespace
    {
        wxString InstallerName(const std::string& installerPath)
        {
            return wxFileName(wxString::FromUTF8(installerPath)).GetFullName();
        }
    } // namespace

    ServerInstallChoice AskInstallOnOtherComputer(wxWindow* parent, const wxString& computerName, bool canSendFiles,
                                                  const std::string& installerPath)
    {
        const wxString title = "Install Marconi Server on the Other Computer";
        const wxString link = wxString::FromUTF8(Marconi::ServerInstaller::kDownloadPage);
        const bool canSend = canSendFiles && !installerPath.empty();

        if (canSend)
        {
            wxMessageDialog dialog(
                parent,
                "Marconi Server is Marconi's own remote desktop server, so you can control " + computerName +
                " without VNC. It is an early version and works on Windows computers only.\n\n"
                "Marconi will copy the installer, " + InstallerName(installerPath) + ", to " + computerName +
                "'s desktop. Then, on " + computerName + "'s screen here in Marconi:\n\n"
                "1. Double-click the installer on its desktop.\n"
                "2. Say yes when Windows asks for permission. (If it asks for an administrator's password, type it "
                "there.)\n"
                "3. Choose an access password when Setup asks. You will type it in Marconi the next time you connect; "
                "it doesn't have to be the same as the VNC password.\n"
                "4. When Setup finishes, close this connection and connect again. Marconi finds Marconi Server by "
                "itself and uses it. Your saved connection stays as it is.\n\n"
                "Nothing is installed until you run the installer there.",
                title, wxYES_NO | wxCANCEL | wxICON_QUESTION);
            dialog.SetYesNoCancelLabels("Copy the Installer", "Copy the Download Link Instead", "Cancel");
            switch (dialog.ShowModal())
            {
            case wxID_YES:
                return ServerInstallChoice::SendInstaller;
            case wxID_NO:
                return ServerInstallChoice::CopyLink;
            default:
                return ServerInstallChoice::Cancel;
            }
        }

        wxString why;
        if (installerPath.empty() && canSendFiles)
        {
            why = "This copy of Marconi doesn't have the Marconi Server installer with it, so it can't copy it for "
                  "you.";
        }
        else
        {
            why = "This connection can't carry files, so Marconi can't copy the installer to " + computerName + ".";
        }
        wxMessageDialog dialog(
            parent,
            why + "\n\nInstead, Marconi can copy the download address to this computer's clipboard. With Share "
            "Clipboard turned on, you can paste it in a web browser on " + computerName + " (the address is " + link +
            "). Download the Marconi Server installer there, double-click it, say yes when Windows asks for "
            "permission and choose an access password. Then close this connection and connect again: Marconi finds "
            "Marconi Server by itself.\n\nMarconi Server works on Windows computers for now.",
            title, wxYES_NO | wxICON_INFORMATION);
        dialog.SetYesNoLabels("Copy the Download Link", "Cancel");
        return dialog.ShowModal() == wxID_YES ? ServerInstallChoice::CopyLink : ServerInstallChoice::Cancel;
    }

    void ShowFinishingSteps(wxWindow* parent, const wxString& computerName, const std::string& installerPath)
    {
        wxMessageBox("Once Marconi has finished copying (the Files panel shows how far it has got), go to " +
                         computerName + "'s screen and double-click " + InstallerName(installerPath) +
                         " on its desktop to install Marconi Server. Choose an access password when Setup asks.\n\n"
                         "Afterwards, close this connection and connect again.",
                     "Install Marconi Server on the Other Computer", wxOK | wxICON_INFORMATION, parent);
    }

    void CopyServerDownloadLink(wxWindow* parent, const wxString& computerName)
    {
        wxClipboardLocker locker;
        if (!locker)
        {
            wxMessageBox("Marconi could not open the clipboard.", "Marconi", wxOK | wxICON_WARNING, parent);
            return;
        }
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(Marconi::ServerInstaller::kDownloadPage)));
        wxTheClipboard->Flush();
        wxMessageBox("The download address is on the clipboard. Paste it into a web browser on " + computerName +
                         ", download the Marconi Server installer and run it there.",
                     "Install Marconi Server on the Other Computer", wxOK | wxICON_INFORMATION, parent);
    }

    void ShareThisComputer(wxWindow* parent)
    {
        const wxString title = "Share This Computer";
#ifdef __WXMSW__
        const std::string installer = FindBundledServerInstaller();
        if (installer.empty())
        {
            wxMessageBox("This copy of Marconi doesn't have the Marconi Server installer with it. Install Marconi with "
                         "its setup program, which includes it, or download Marconi Server from " +
                             wxString::FromUTF8(Marconi::ServerInstaller::kDownloadPage) + ".",
                         title, wxOK | wxICON_INFORMATION, parent);
            return;
        }
        wxMessageDialog ask(parent,
                            "Marconi Server lets other computers see and control this one with Marconi.\n\n"
                            "Setup asks Windows for permission, then asks you to choose an access password. Anyone who "
                            "knows the password can control this computer, so choose one you would not mind guarding. "
                            "Setup also lets Marconi Server through Windows Firewall, and starts it whenever someone "
                            "signs in.\n\nStart Setup now?",
                            title, wxYES_NO | wxICON_QUESTION);
        ask.SetYesNoLabels("Start Setup", "Cancel");
        if (ask.ShowModal() != wxID_YES)
        {
            return;
        }
        // ShellExecute (not a plain process start), so Windows can show its permission prompt for the installer.
        if (!wxLaunchDefaultApplication(wxString::FromUTF8(installer)))
        {
            LOG_WARNING("App", "Could not start the Marconi Server installer at %s", installer.c_str());
            wxMessageBox("Marconi could not start the installer, " + wxString::FromUTF8(installer) + ".", title,
                         wxOK | wxICON_WARNING, parent);
        }
#else
        wxMessageBox("Marconi Server's installer is for Windows for now, so there is nothing to start on this "
                     "computer.",
                     title, wxOK | wxICON_INFORMATION, parent);
#endif
    }
} // namespace MarconiUI
