// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: F8 > Install Marconi Server on the Other Computer

#include "ViewerFrame.h"

#include "FilePanel.h"
#include "ServerInstall.h"

#include <wx/msgdlg.h>

namespace MarconiUI
{
    void ViewerFrame::InstallServerOnOtherComputer()
    {
        if (!m_connected)
        {
            return;
        }
        const wxString name = GetServerDisplayName();
        if (m_rfb == nullptr)
        {
            wxMessageBox("Marconi installs Marconi Server through a VNC connection, which can carry the installer "
                         "across. This connection isn't one.",
                         "Install Marconi Server on the Other Computer", wxOK | wxICON_INFORMATION, this);
            return;
        }
        // TightVNC carries files on the connection itself; a connection through SSH uses SFTP (the file panel logs in
        // by itself). Other VNC servers can't take files.
        const bool canSendFiles = m_rfb->HasFileTransfer() || m_options.sshTunnel;
        const std::string installer = FindBundledServerInstaller();
        switch (AskInstallOnOtherComputer(this, name, canSendFiles, installer))
        {
        case ServerInstallChoice::SendInstaller:
            UploadToRemoteDesktop({installer});
            ShowFinishingSteps(this, name, installer);
            break;
        case ServerInstallChoice::CopyLink:
            CopyServerDownloadLink(this, name);
            break;
        case ServerInstallChoice::Cancel:
            break;
        }
    }
} // namespace MarconiUI
