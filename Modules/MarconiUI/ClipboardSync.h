// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps the local clipboard and the remote desktop's clipboard in step: text, and images where the server takes them

#pragma once

#include <MarconiCore/RemoteSession.h>

#include <wx/string.h>

#include <functional>
#include <string>

namespace MarconiUI
{
    class ClipboardSync
    {
    public:
        ClipboardSync() = default;

        // The connection the clipboard travels over; null until the viewer has made one. UI thread.
        void SetSession(Marconi::RemoteSession* session) { m_session = session; }

        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }

        // Short messages for the status bar, e.g. when characters can't be sent. UI thread.
        void SetStatusHandler(std::function<void(const wxString&)> handler) { m_status = std::move(handler); }

        // UI thread. The server's clipboard changed: Latin-1 text from a server without the extended clipboard, or
        // text and/or an image over it. Put on the local clipboard while sharing is on; kept for GetNow either way.
        void OnServerText(const std::string& latin1Text);
        void OnServerContent(const Marconi::Rfb::ClipboardContent& content);

        // UI thread. Sends the local clipboard if it changed since the last exchange in either direction. Call
        // only while the viewer is in front: when it gains focus, which is when someone who copied locally is about
        // to paste remotely, and when the clipboard changes while it stays in front.
        void SendIfChanged();

        // UI thread. The explicit actions, which work with sharing off too. SendNow sends the local clipboard as it
        // is. GetNow asks a server with the extended clipboard for its clipboard, or puts the last text another
        // server sent back on the local clipboard.
        void SendNow();
        void GetNow();

        // Forget what was exchanged (new connection: the server's clipboard is unknown).
        void Reset();

    private:
        bool ReadLocal(Marconi::Rfb::ClipboardContent& out) const;
        void Send(const Marconi::Rfb::ClipboardContent& content, bool explicitly);
        void Apply(const Marconi::Rfb::ClipboardContent& content);
        void Report(const wxString& message);

        Marconi::RemoteSession* m_session = nullptr;
        bool m_enabled = true;
        std::function<void(const wxString&)> m_status;
        Marconi::Rfb::ClipboardContent m_lastExchanged; // text with local line endings as "\n"
        Marconi::Rfb::ClipboardContent m_lastFromServer;
        bool m_applyNextFromServer = false; // GetNow asked with sharing off: apply the answer anyway
    };
} // namespace MarconiUI
