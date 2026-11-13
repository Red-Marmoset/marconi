// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The connections saved in Marconi, as MarconiMcp's tools see them: everything but the password, which only the
// connection itself asks for and which never reaches the AI

#pragma once

#include <string>
#include <vector>

namespace Marconi
{
    namespace Mcp
    {
        struct SavedComputer
        {
            std::string id;      // the saved connection's key, never shown
            std::string name;    // as the user named it; empty means the address is its name
            std::string address; // as typed in Marconi
            std::string detected; // what Marconi's last probe found: "vnc 5900", "marconi 1895", "rdp 3389", or empty
            std::string username;
            std::string group;
            std::string notes;
            std::string quality;     // "best", "balanced" or "low"
            std::string certificate; // the SHA-256 of a certificate the user trusted, as hex; empty for none
            std::string sshHostKey;  // an SSH host key the user trusted ("SHA256:..."); empty for none
            bool sshTunnel = false;
            bool favourite = false;
            bool hasPassword = false; // a password is saved and AI tools may use it

            const std::string& DisplayName() const { return name.empty() ? address : name; }
        };

        // Marconi's saved connections, minus the ones hidden from AI tools. MarconiMcp's program reads them through
        // the app's own code (MarconiConnections); tests give a list of their own.
        class SavedComputers
        {
        public:
            virtual ~SavedComputers() = default;
            virtual std::vector<SavedComputer> List() = 0;
            // The saved password, only while AI tools may use saved passwords. False if there's none.
            virtual bool Password(const std::string& id, std::string& out) = 0;
        };

        // The saved connection `text` names: its name or its address, ignoring case. Null if none or several match.
        const SavedComputer* FindSavedComputer(const std::vector<SavedComputer>& computers, const std::string& text,
                                               std::string& error);

        // The address to connect to: the saved one, turned into the protocol and port Marconi found last time
        // ("studio" that answered as a Marconi Server on 1900 becomes "marconi://studio:1900"). False, with `error`,
        // for Remote Desktop, which MarconiMcp can't use yet.
        bool AddressFor(const SavedComputer& computer, std::string& address, std::string& error);
    } // namespace Mcp
} // namespace Marconi
