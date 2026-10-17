// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Saved connections: settings in wxConfig, passwords in the OS keychain (wxSecretStore)

#pragma once

#include "VncCanvas.h"

#include <wx/datetime.h>
#include <wx/string.h>

#include <vector>

namespace MarconiUI
{
    struct SavedConnection
    {
        wxString id; // stable config key, never shown
        wxString name;
        wxString address; // as typed: host, host:display, host::port
        wxString detected; // what the last probe found, e.g. "vnc 5900"; empty = probe next time
        wxString username;
        bool rememberPassword = false;
        wxString dropFolder;  // TightVNC: where files dropped on the remote screen go, e.g. "/C:/Users/Public/Desktop"
        wxString sshHostKey;  // fingerprint of the SSH host key trusted for file transfer, e.g. "SHA256:..."
        wxString layout;      // where the viewer's panels were: a wxAUI perspective; empty = the default layout
        wxString certificate; // SHA-256 of a server certificate the user chose to trust, as hex; empty = none
        ScaleMode scaleMode = ScaleMode::Fit;
        int zoomPercent = 100; // with ScaleMode::Fixed
        long displayId = 0;    // the remote display to show, by the id the server gives it; 0 for all
        bool viewOnly = false;
        bool clipboard = true;
        bool systemKeys = false; // send system shortcuts (Alt+Tab, the Windows key, Cmd+Tab) to the other computer
        wxString quality = "balanced"; // picture quality: "best", "balanced" or "low" (Marconi::QualityName)
        bool lockOnDisconnect = false;  // lock the other computer's screen when the window closes
        bool favourite = false;         // listed first
        wxString notes;                 // anything worth remembering about the computer
        wxString group;                 // optional, e.g. "Studio": the list keeps a group's connections together
        wxString macAddress;            // hardware address, for waking it up (Wake-on-LAN)
        wxRect windowRect;              // where the viewer window was; empty = not remembered
        wxDateTime lastUsed;

        wxString GetDisplayName() const { return name.empty() ? address : name; }
    };

    class ConnectionStore
    {
    public:
        // Most recently used first.
        std::vector<SavedConnection> LoadAll() const;
        bool Load(const wxString& id, SavedConnection& out) const;
        // Adds or updates; assigns an id to new connections.
        void Save(SavedConnection& connection);
        void Remove(const wxString& id);
        // The saved connection with this address (case-insensitive), if any.
        bool FindByAddress(const wxString& address, SavedConnection& out) const;
        void MarkUsed(const wxString& id);

        // Keychain. Return false if there is no stored password or no keychain on this system.
        static bool IsKeychainAvailable();
        bool LoadPassword(const wxString& id, wxString& password) const;
        bool SavePassword(const wxString& id, const wxString& username, const wxString& password);
        void DeletePassword(const wxString& id);

        // Marconi used to be called rmvnc. Copies rmvnc's saved connections and passwords once, leaving the originals.
        static void MigrateFromRmvnc();
    };
} // namespace MarconiUI
