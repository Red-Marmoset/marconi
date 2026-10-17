// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Files on the local clipboard, as Explorer and Finder put them there with Copy and take them with Paste

#include "ClipboardFiles.h"

#include <wx/clipbrd.h>
#include <wx/dataobj.h>

namespace MarconiUI
{
    bool ClipboardHasFiles()
    {
        wxClipboardLocker locker;
        if (!locker)
        {
            return false;
        }
        return wxTheClipboard->IsSupported(wxDF_FILENAME);
    }

    std::vector<std::string> GetClipboardFiles()
    {
        std::vector<std::string> paths;
        wxClipboardLocker locker;
        if (!locker || !wxTheClipboard->IsSupported(wxDF_FILENAME))
        {
            return paths;
        }
        wxFileDataObject data;
        if (!wxTheClipboard->GetData(data))
        {
            return paths;
        }
        for (wxString file : data.GetFilenames())
        {
            file.Replace("\\", "/");
            if (!file.empty())
            {
                paths.push_back(file.utf8_string());
            }
        }
        return paths;
    }

    bool SetClipboardFiles(const std::vector<std::string>& paths)
    {
        wxClipboardLocker locker;
        if (!locker)
        {
            return false;
        }
        wxFileDataObject* data = new wxFileDataObject();
        for (const std::string& path : paths)
        {
            data->AddFile(wxString::FromUTF8(path));
        }
        bool ok = wxTheClipboard->SetData(data);
        wxTheClipboard->Flush(); // keep the list available after Marconi exits
        return ok;
    }
} // namespace MarconiUI
