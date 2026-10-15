// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps the local clipboard and the remote desktop's clipboard in step (text only)

#include "ClipboardSync.h"

#include <RmVncCore/Text.h>

#include <Log/Log.h>

#include <wx/clipbrd.h>
#include <wx/dataobj.h>

namespace RmVncUI
{
    namespace
    {
        constexpr size_t kMaxClipboardBytes = 4 * 1024 * 1024;
    }

    void ClipboardSync::OnServerText(const std::string& latin1Text)
    {
        if (!m_enabled)
        {
            return;
        }
        std::string utf8 = RmVnc::ToNetworkLineEndings(RmVnc::Latin1ToUtf8(latin1Text));
        if (utf8 == m_lastExchanged)
        {
            return;
        }
        m_lastExchanged = utf8;
        wxClipboardLocker locker;
        if (!locker)
        {
            LOG_WARNING("Clipboard", "Could not open the clipboard to store the server's text");
            return;
        }
        // wxTextDataObject converts "\n" to the platform's line endings.
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(utf8)));
        wxTheClipboard->Flush(); // keep the text available after rmvnc exits
    }

    void ClipboardSync::SendIfChanged()
    {
        if (!m_enabled || !m_client.IsConnected())
        {
            return;
        }
        wxString text;
        {
            wxClipboardLocker locker;
            if (!locker || !wxTheClipboard->IsSupported(wxDF_UNICODETEXT))
            {
                return;
            }
            wxTextDataObject data;
            if (!wxTheClipboard->GetData(data))
            {
                return;
            }
            text = data.GetText();
        }
        std::string utf8 = RmVnc::ToNetworkLineEndings(text.utf8_string());
        if (utf8.empty() || utf8 == m_lastExchanged || utf8.size() > kMaxClipboardBytes)
        {
            return;
        }
        m_lastExchanged = utf8;
        m_client.SendClientCutText(RmVnc::Utf8ToLatin1(utf8));
    }
} // namespace RmVncUI
