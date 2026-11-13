// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiMcp's view of the connections saved in Marconi, read through the app's own ConnectionStore

#include "StoreComputers.h"

#include <MarconiConnections/ConnectionStore.h>

namespace MarconiMcpApp
{
    std::vector<Marconi::Mcp::SavedComputer> StoreComputers::List()
    {
        MarconiUI::ConnectionStore store;
        const bool passwordsAllowed = MarconiUI::ConnectionStore::AiToolsMayUsePasswords();
        std::vector<Marconi::Mcp::SavedComputer> computers;
        for (const MarconiUI::SavedConnection& saved : store.LoadAll())
        {
            if (saved.hideFromAiTools)
            {
                continue;
            }
            Marconi::Mcp::SavedComputer computer;
            computer.id = saved.id.utf8_string();
            computer.name = saved.name.utf8_string();
            computer.address = saved.address.utf8_string();
            computer.detected = saved.detected.utf8_string();
            computer.username = saved.username.utf8_string();
            computer.group = saved.group.utf8_string();
            computer.notes = saved.notes.utf8_string();
            computer.quality = saved.quality.utf8_string();
            computer.certificate = saved.certificate.utf8_string();
            computer.sshHostKey = saved.sshHostKey.utf8_string();
            computer.sshTunnel = saved.sshTunnel;
            computer.favourite = saved.favourite;
            computer.hasPassword = passwordsAllowed && saved.rememberPassword;
            computers.push_back(computer);
        }
        return computers;
    }

    bool StoreComputers::Password(const std::string& id, std::string& out)
    {
        if (!MarconiUI::ConnectionStore::AiToolsMayUsePasswords())
        {
            return false;
        }
        MarconiUI::ConnectionStore store;
        MarconiUI::SavedConnection saved;
        const wxString key = wxString::FromUTF8(id);
        if (!store.Load(key, saved) || saved.hideFromAiTools || !saved.rememberPassword)
        {
            return false;
        }
        wxString password;
        if (!store.LoadPassword(key, password))
        {
            return false;
        }
        out = password.utf8_string();
        return true;
    }
} // namespace MarconiMcpApp
