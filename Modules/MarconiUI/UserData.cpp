// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Removing everything Marconi keeps for the current user: saved connections, their passwords, settings and logs

#include "UserData.h"

#include "ConnectionStore.h"

#include <wx/config.h>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

#include <vector>

namespace MarconiUI
{
    namespace
    {
        bool s_removeLogsOnExit = false;
    }

    wxString GetLogDirectory()
    {
        return wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs";
    }

    void RemoveAllUserData()
    {
        // Connections one by one, so each one's keychain password goes with it.
        ConnectionStore store;
        for (const SavedConnection& connection : store.LoadAll())
        {
            store.Remove(connection.id);
        }
        // Then the settings, apart from the note that rmvnc's old data was already copied: without it, that data would
        // come back at the next start.
        wxConfigBase* config = wxConfigBase::Get();
        config->DeleteGroup("/Connections");
        config->DeleteGroup("/Appearance");
        config->Flush();

        wxString logDir = GetLogDirectory();
        if (wxDirExists(logDir))
        {
            std::vector<wxString> files;
            wxDir dir(logDir);
            wxString name;
            for (bool more = dir.GetFirst(&name, wxEmptyString, wxDIR_FILES); more; more = dir.GetNext(&name))
            {
                files.push_back(logDir + wxFILE_SEP_PATH + name);
            }
            wxLogNull quiet; // the log file in use can't be removed on Windows; the folder goes at exit
            for (const wxString& file : files)
            {
                wxRemoveFile(file);
            }
            s_removeLogsOnExit = true;
        }
    }

    bool ShouldRemoveLogsOnExit()
    {
        return s_removeLogsOnExit;
    }

    void RemoveLogDirectory()
    {
        wxLogNull quiet;
        wxFileName::Rmdir(GetLogDirectory(), wxPATH_RMDIR_RECURSIVE);
        s_removeLogsOnExit = false;
    }
} // namespace MarconiUI
