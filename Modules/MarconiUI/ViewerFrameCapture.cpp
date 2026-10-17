// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: screenshots, headless snapshots, and keeping the clipboards in step

#include "ViewerFrame.h"

#include "ClipboardFiles.h"
#include "ClipboardWatch.h"
#include "FilePanel.h"
#include "ForegroundWindow.h"
#include "Screenshot.h"
#include "ViewerCommands.h"

#include <Log/Log.h>

#include <wx/clipbrd.h>
#include <wx/config.h>
#include <wx/dataobj.h>
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <mutex>

namespace MarconiUI
{
    void ViewerFrame::SaveHeadlessSnapshot()
    {
        // What a viewer window would be showing right now, plus how that relates to the server's desktop.
        wxImage image = m_canvas->GetShownPicture();
        if (!image.IsOk())
        {
            LOG_INFO("Viewer", "Headless snapshot: nothing shown yet");
            return;
        }
        int serverWidth;
        int serverHeight;
        {
            Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            serverWidth = framebuffer.GetWidth();
            serverHeight = framebuffer.GetHeight();
        }
        ++m_headlessSnapshots;
        wxString path = wxStandardPaths::Get().GetUserLocalDataDir() + wxFILE_SEP_PATH + "logs" + wxFILE_SEP_PATH +
                        wxDateTime::Now().Format("headless_%Y-%m-%d_%H-%M-%S_") +
                        wxString::Format("%03d.png", m_headlessSnapshots);
        LOG_INFO("Viewer", "Headless snapshot %s: showing %dx%d (%s), server desktop %dx%d", path.utf8_string().c_str(),
                 image.GetWidth(), image.GetHeight(),
                 m_canvas->IsShowingCurrentPicture() ? "current" : "previous picture, new one incomplete", serverWidth,
                 serverHeight);
        SavePngInBackground(image, path);
    }

    void ViewerFrame::SavePngInBackground(const wxImage& image, const wxString& path, std::function<void(bool)> done)
    {
        // Encoding a 4K PNG takes seconds; doing it here would stall the viewer. The thread gets its own deep copy
        // (wxImage's reference counting isn't thread-safe) and is joined when the window goes away.
        wxImage copy = image.Copy();
        copy.SetOption(wxIMAGE_OPTION_PNG_COMPRESSION_LEVEL, 1);
        m_saveThreads.emplace_back([this, copy, path, done]() mutable {
            bool saved = copy.SaveFile(path, wxBITMAP_TYPE_PNG);
            if (!saved)
            {
                LOG_WARNING("Viewer", "Could not save %s", path.utf8_string().c_str());
            }
            if (done)
            {
                CallAfter([done, saved]() { done(saved); });
            }
        });
    }

    bool ViewerFrame::IncludePointerInScreenshots()
    {
        return wxConfigBase::Get()->ReadBool("/Screenshots/IncludePointer", false);
    }

    wxMenu* ViewerFrame::CreateScreenshotMenu() const
    {
        wxMenu* menu = new wxMenu();
        menu->Append(ID_SCREENSHOT_SAVE, "&Save Screenshot...");
        menu->Append(ID_SCREENSHOT_COPY, "&Copy Screenshot");
        menu->AppendSeparator();
        menu->AppendCheckItem(ID_SCREENSHOT_POINTER, "Include the Mouse &Pointer",
                              "Servers that draw their own pointer into the picture include it either way");
        menu->Check(ID_SCREENSHOT_POINTER, IncludePointerInScreenshots());
        bool havePicture = m_connected && m_canvas->HasPicture();
        menu->Enable(ID_SCREENSHOT_SAVE, havePicture);
        menu->Enable(ID_SCREENSHOT_COPY, havePicture);
        return menu;
    }

    wxImage ViewerFrame::TakeScreenshot() const
    {
        // The whole remote desktop at its own resolution, never the window around it.
        wxImage picture = m_canvas->GetShownPicture();
        if (IncludePointerInScreenshots() && m_canvas->HasRemoteCursor() && m_canvas->GetLastPointer().x >= 0)
        {
            wxPoint pointer = m_canvas->GetLastPointer() - m_canvas->GetPictureRect().GetPosition();
            return ComposeScreenshot(picture, &m_canvas->GetRemoteCursor(), pointer);
        }
        return picture;
    }

    void ViewerFrame::SaveScreenshot()
    {
        wxImage image = TakeScreenshot();
        if (!image.IsOk())
        {
            SetStatusText("Nothing to save yet: no picture has arrived");
            return;
        }
        wxString folder = m_screenshotFolder.empty() ? wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Pictures)
                                                     : m_screenshotFolder;
        wxString name = SuggestScreenshotName(GetServerDisplayName(), wxDateTime::Now());
        m_canvas->ReleaseInput();
        wxFileDialog dialog(this, "Save Screenshot", folder, name, "PNG image (*.png)|*.png",
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK)
        {
            return;
        }
        wxString path = dialog.GetPath();
        m_screenshotFolder = dialog.GetDirectory();
        SetStatusText("Saving " + path + "...");
        SavePngInBackground(image, path, [this, path](bool saved) {
            if (saved)
            {
                SetStatusText("Saved " + path);
            }
            else
            {
                SetStatusText("Could not save " + path);
                wxMessageBox("The screenshot could not be saved to:\n\n" + path, "Marconi", wxOK | wxICON_ERROR, this);
            }
        });
    }

    void ViewerFrame::CopyScreenshot()
    {
        wxImage image = TakeScreenshot();
        if (!image.IsOk())
        {
            SetStatusText("Nothing to copy yet: no picture has arrived");
            return;
        }
        {
            wxClipboardLocker locker;
            if (!locker)
            {
                SetStatusText("Could not open the clipboard");
                return;
            }
            wxTheClipboard->SetData(new wxBitmapDataObject(wxBitmap(image)));
            wxTheClipboard->Flush();
        }
        // With clipboard sharing on and a server that takes images, the copy travels like any other.
        bool shared = m_clipboard.IsEnabled() && m_client.ServerAcceptsClipboardImages();
        SyncClipboardToServer();
        SetStatusText(shared ? "Screenshot copied, and sent to the other computer's clipboard"
                             : "Screenshot copied (to this computer's clipboard only)");
    }

    void ViewerFrame::UploadToRemoteDesktop(const std::vector<std::string>& paths)
    {
        if (paths.empty())
        {
            return;
        }
        ShowFiles();
        m_files->UploadToDesktop(paths);
        SetStatusText(wxString::Format("Copying %zu item%s to the remote Desktop", paths.size(),
                                       paths.size() == 1 ? "" : "s"));
    }

    void ViewerFrame::PasteFilesFromClipboard()
    {
        // Files copied in Explorer or Finder: pasting on the remote screen copies them to its Desktop, as a drop does.
        std::vector<std::string> files = GetClipboardFiles();
        if (files.empty())
        {
            SetStatusText("The clipboard holds no files to paste");
            return;
        }
        UploadToRemoteDesktop(files);
    }

    void ViewerFrame::SyncClipboardToServer()
    {
        // Only while the user is working in this window: what they copy in other programs (a password, say) isn't
        // meant for the remote computer. Coming back to the viewer sends the clipboard as it is then.
        if (m_connected && !m_canvas->IsViewOnly() && IsForegroundWindow(*this))
        {
            m_clipboard.SendIfChanged();
        }
    }

    void ViewerFrame::WatchClipboard()
    {
        // Copying while the viewer is in front doesn't change focus, so watch for changes rather than waiting
        // for the window to be activated again. The count is recorded even when nothing is sent (another window
        // is in front), so a change made elsewhere is never sent later on its account: activation sends the
        // clipboard once instead.
        long count = GetClipboardChangeCount();
        if (count >= 0 && count != m_clipboardChangeCount)
        {
            m_clipboardChangeCount = count;
            SyncClipboardToServer();
        }
    }
} // namespace MarconiUI
