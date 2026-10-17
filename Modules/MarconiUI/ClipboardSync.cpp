// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps the local clipboard and the remote desktop's clipboard in step: text, and images where the server takes them

#include "ClipboardSync.h"

#include "Dib.h"

#include <MarconiCore/Text.h>

#include <Log/Log.h>

#include <wx/bitmap.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>

namespace MarconiUI
{
    namespace
    {
        constexpr size_t kMaxClipboardBytes = Marconi::Rfb::ClipboardChannel::kMaxTextBytes;

        bool SameContent(const Marconi::Rfb::ClipboardContent& a, const Marconi::Rfb::ClipboardContent& b)
        {
            return a.utf8Text == b.utf8Text && a.dib == b.dib;
        }
    } // namespace

    void ClipboardSync::Reset()
    {
        m_lastExchanged = Marconi::Rfb::ClipboardContent();
        m_lastFromServer = Marconi::Rfb::ClipboardContent();
        m_applyNextFromServer = false;
    }

    void ClipboardSync::Report(const wxString& message)
    {
        if (m_status)
        {
            m_status(message);
        }
    }

    void ClipboardSync::OnServerText(const std::string& latin1Text)
    {
        Marconi::Rfb::ClipboardContent content;
        content.utf8Text = Marconi::ToNetworkLineEndings(Marconi::Latin1ToUtf8(latin1Text));
        OnServerContent(content);
    }

    void ClipboardSync::OnServerContent(const Marconi::Rfb::ClipboardContent& content)
    {
        Marconi::Rfb::ClipboardContent normalised = content;
        normalised.utf8Text = Marconi::ToNetworkLineEndings(normalised.utf8Text);
        m_lastFromServer = normalised;
        bool apply = m_enabled || m_applyNextFromServer;
        m_applyNextFromServer = false;
        if (!apply || SameContent(normalised, m_lastExchanged))
        {
            return;
        }
        Apply(normalised);
    }

    void ClipboardSync::Apply(const Marconi::Rfb::ClipboardContent& content)
    {
        wxClipboardLocker locker;
        if (!locker)
        {
            LOG_WARNING("Clipboard", "Could not open the clipboard to store the server's clipboard");
            return;
        }
        // wxTextDataObject converts "\n" to the platform's line endings.
        wxDataObjectComposite* composite = new wxDataObjectComposite();
        if (!content.utf8Text.empty())
        {
            composite->Add(new wxTextDataObject(wxString::FromUTF8(content.utf8Text)), true);
        }
        if (!content.dib.empty())
        {
            wxImage image = DibToImage(content.dib);
            if (image.IsOk())
            {
                composite->Add(new wxBitmapDataObject(wxBitmap(image)), content.utf8Text.empty());
            }
            else
            {
                LOG_WARNING("Clipboard", "The server sent an image in a form Marconi can't read (%zu bytes)",
                            content.dib.size());
            }
        }
        if (composite->GetFormatCount() == 0)
        {
            delete composite;
            return;
        }
        m_lastExchanged = content;
        wxTheClipboard->SetData(composite);
        wxTheClipboard->Flush(); // keep it available after Marconi exits
    }

    bool ClipboardSync::ReadLocal(Marconi::Rfb::ClipboardContent& out) const
    {
        wxClipboardLocker locker;
        if (!locker)
        {
            return false;
        }
        if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT))
        {
            wxTextDataObject data;
            if (wxTheClipboard->GetData(data))
            {
                out.utf8Text = Marconi::ToNetworkLineEndings(data.GetText().utf8_string());
            }
        }
        // Images cost more to read, so only when there is somewhere for them to go.
        if (out.utf8Text.empty() && m_client.ServerAcceptsClipboardImages() && wxTheClipboard->IsSupported(wxDF_BITMAP))
        {
            wxBitmapDataObject data;
            if (wxTheClipboard->GetData(data))
            {
                out.dib = ImageToDib(data.GetBitmap().ConvertToImage());
            }
        }
        return !out.IsEmpty();
    }

    void ClipboardSync::SendIfChanged()
    {
        if (!m_enabled || !m_client.IsConnected())
        {
            return;
        }
        Marconi::Rfb::ClipboardContent content;
        if (!ReadLocal(content) || SameContent(content, m_lastExchanged))
        {
            return;
        }
        Send(content, false);
    }

    void ClipboardSync::SendNow()
    {
        if (!m_client.IsConnected())
        {
            return;
        }
        Marconi::Rfb::ClipboardContent content;
        if (!ReadLocal(content))
        {
            Report("Nothing to send: the clipboard is empty");
            return;
        }
        Send(content, true);
    }

    void ClipboardSync::Send(const Marconi::Rfb::ClipboardContent& content, bool explicitly)
    {
        if (content.utf8Text.size() > kMaxClipboardBytes || content.dib.size() > Marconi::Rfb::ClipboardChannel::kMaxDibBytes)
        {
            if (explicitly)
            {
                Report("The clipboard is too large to send");
            }
            return;
        }
        m_lastExchanged = content;
        if (m_client.OfferClipboard(content))
        {
            if (explicitly)
            {
                Report(content.utf8Text.empty() ? "Sent the image to the other computer; paste it there"
                                                : "Sent the text to the other computer; paste it there");
            }
            return;
        }
        if (content.utf8Text.empty())
        {
            if (explicitly)
            {
                Report("The other computer can't take images from the clipboard");
            }
            return;
        }
        std::string latin1 = Marconi::Utf8ToLatin1(content.utf8Text);
        if (Marconi::Latin1ToUtf8(latin1) != content.utf8Text)
        {
            Report("Some characters couldn't be sent: the other computer only takes Western European text");
        }
        else if (explicitly)
        {
            Report("Sent the text to the other computer; paste it there");
        }
        m_client.SendClientCutText(latin1);
    }

    void ClipboardSync::GetNow()
    {
        if (!m_client.IsConnected())
        {
            return;
        }
        if (m_client.HasExtendedClipboard())
        {
            m_applyNextFromServer = true;
            m_lastExchanged = Marconi::Rfb::ClipboardContent(); // apply whatever comes, even if seen before
            m_client.RequestServerClipboard();
            Report("Asked the other computer for its clipboard");
            return;
        }
        if (m_lastFromServer.IsEmpty())
        {
            Report("The other computer hasn't copied any text yet");
            return;
        }
        m_lastExchanged = Marconi::Rfb::ClipboardContent();
        Apply(m_lastFromServer);
        Report("Got the text the other computer last copied");
    }
} // namespace MarconiUI
