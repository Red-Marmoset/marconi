// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The windows for putting Marconi Server on a computer: the instructions for the other computer (F8 menu) and running
// the installer on this one (Settings > Share this computer)

#pragma once

#include <wx/string.h>

#include <string>

class wxWindow;

namespace MarconiUI
{
    // The Marconi Server installer that came with Marconi (next to the program, or in the build folder of a
    // development build); an empty string if there is none.
    std::string FindBundledServerInstaller();

    enum class ServerInstallChoice
    {
        Cancel,
        SendInstaller, // copy the installer to the other computer
        CopyLink,      // copy the download link to the clipboard
    };

    // Explains, in plain words, what installing Marconi Server on `computerName` involves and asks how to go on.
    // `canSendFiles` says whether this connection can carry files; `installerPath` is the installer to send (empty if
    // there is none). Only the choices that can work are offered.
    ServerInstallChoice AskInstallOnOtherComputer(wxWindow* parent, const wxString& computerName, bool canSendFiles,
                                                  const std::string& installerPath);

    // What to do once the installer has been copied: the finishing steps, on the other computer's own screen.
    void ShowFinishingSteps(wxWindow* parent, const wxString& computerName, const std::string& installerPath);

    // Copies the download page's address to the clipboard and says where to paste it.
    void CopyServerDownloadLink(wxWindow* parent, const wxString& computerName);

    // Runs the bundled installer on this computer (Windows only for now), after saying what it will do.
    void ShareThisComputer(wxWindow* parent);
} // namespace MarconiUI
