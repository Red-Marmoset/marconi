// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeps the local clipboard and the remote desktop's clipboard in step (text only)

#pragma once

#include <RmVncCore/Rfb/Client.h>

#include <string>

namespace RmVncUI
{
    class ClipboardSync
    {
    public:
        explicit ClipboardSync(RmVnc::Rfb::Client& client) : m_client(client) {}

        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }

        // UI thread. The server's clipboard changed: put the text on the local clipboard.
        void OnServerText(const std::string& latin1Text);

        // UI thread. Sends the local clipboard if it changed since the last exchange in either direction. Call
        // when the viewer gains focus, which is when someone who copied locally is about to paste remotely.
        void SendIfChanged();

        // Forget the last exchanged text (new connection: the server's clipboard is unknown).
        void Reset() { m_lastExchanged.clear(); }

    private:
        RmVnc::Rfb::Client& m_client;
        bool m_enabled = true;
        std::string m_lastExchanged; // UTF-8, local line endings stripped to "\n"
    };
} // namespace RmVncUI
