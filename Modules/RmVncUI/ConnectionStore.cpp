// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Saved connections: settings in wxConfig, passwords in the OS keychain (wxSecretStore)

#include "ConnectionStore.h"

#include <Log/Log.h>

#include <wx/config.h>
#include <wx/secretstore.h>

#include <algorithm>

namespace RmVncUI
{
    namespace
    {
        const wxString kRoot = "/Connections";

        wxString PathFor(const wxString& id)
        {
            return kRoot + "/" + id;
        }

        wxString ServiceFor(const wxString& id)
        {
            return "rmvnc/" + id;
        }
    } // namespace

    std::vector<SavedConnection> ConnectionStore::LoadAll() const
    {
        std::vector<SavedConnection> connections;
        wxConfigBase* config = wxConfigBase::Get();
        wxString oldPath = config->GetPath();
        config->SetPath(kRoot);
        std::vector<wxString> ids;
        wxString group;
        long cookie = 0;
        for (bool more = config->GetFirstGroup(group, cookie); more; more = config->GetNextGroup(group, cookie))
        {
            ids.push_back(group);
        }
        config->SetPath(oldPath);

        for (const wxString& id : ids)
        {
            SavedConnection connection;
            if (Load(id, connection))
            {
                connections.push_back(connection);
            }
        }
        std::sort(connections.begin(), connections.end(), [](const SavedConnection& a, const SavedConnection& b) {
            if (a.lastUsed.IsValid() != b.lastUsed.IsValid())
            {
                return a.lastUsed.IsValid();
            }
            if (a.lastUsed.IsValid() && a.lastUsed != b.lastUsed)
            {
                return a.lastUsed > b.lastUsed;
            }
            return a.GetDisplayName().CmpNoCase(b.GetDisplayName()) < 0;
        });
        return connections;
    }

    bool ConnectionStore::Load(const wxString& id, SavedConnection& out) const
    {
        wxConfigBase* config = wxConfigBase::Get();
        wxString path = PathFor(id);
        if (!config->HasGroup(path))
        {
            return false;
        }
        out = SavedConnection();
        out.id = id;
        out.name = config->Read(path + "/Name", wxEmptyString);
        out.address = config->Read(path + "/Address", wxEmptyString);
        out.username = config->Read(path + "/Username", wxEmptyString);
        out.rememberPassword = config->ReadBool(path + "/RememberPassword", false);
        out.scaleMode = config->Read(path + "/Scale", "fit") == "100" ? ScaleMode::Actual : ScaleMode::Fit;
        out.viewOnly = config->ReadBool(path + "/ViewOnly", false);
        out.clipboard = config->ReadBool(path + "/Clipboard", true);
        long long lastUsed = config->ReadLongLong(path + "/LastUsed", 0);
        if (lastUsed > 0)
        {
            out.lastUsed = wxDateTime(static_cast<time_t>(lastUsed));
        }
        return !out.address.empty();
    }

    void ConnectionStore::Save(SavedConnection& connection)
    {
        wxConfigBase* config = wxConfigBase::Get();
        if (connection.id.empty())
        {
            // Short, stable and unique enough for a personal address book.
            for (long n = wxDateTime::Now().GetTicks() % 1000000;; ++n)
            {
                wxString candidate = wxString::Format("c%ld", n);
                if (!config->HasGroup(PathFor(candidate)))
                {
                    connection.id = candidate;
                    break;
                }
            }
        }
        wxString path = PathFor(connection.id);
        config->Write(path + "/Name", connection.name);
        config->Write(path + "/Address", connection.address);
        config->Write(path + "/Username", connection.username);
        config->Write(path + "/RememberPassword", connection.rememberPassword);
        config->Write(path + "/Scale", connection.scaleMode == ScaleMode::Actual ? "100" : "fit");
        config->Write(path + "/ViewOnly", connection.viewOnly);
        config->Write(path + "/Clipboard", connection.clipboard);
        config->Write(path + "/LastUsed",
                      connection.lastUsed.IsValid() ? static_cast<long long>(connection.lastUsed.GetTicks()) : 0LL);
        config->Flush();
        if (!connection.rememberPassword)
        {
            DeletePassword(connection.id);
        }
    }

    void ConnectionStore::Remove(const wxString& id)
    {
        if (id.empty())
        {
            return;
        }
        DeletePassword(id);
        wxConfigBase::Get()->DeleteGroup(PathFor(id));
        wxConfigBase::Get()->Flush();
    }

    bool ConnectionStore::FindByAddress(const wxString& address, SavedConnection& out) const
    {
        for (const SavedConnection& connection : LoadAll())
        {
            if (connection.address.CmpNoCase(address.Strip(wxString::both)) == 0)
            {
                out = connection;
                return true;
            }
        }
        return false;
    }

    void ConnectionStore::MarkUsed(const wxString& id)
    {
        SavedConnection connection;
        if (Load(id, connection))
        {
            connection.lastUsed = wxDateTime::Now();
            Save(connection);
        }
    }

    bool ConnectionStore::IsKeychainAvailable()
    {
        wxString error;
        return wxSecretStore::GetDefault().IsOk(&error);
    }

    bool ConnectionStore::LoadPassword(const wxString& id, wxString& password) const
    {
        wxSecretStore store = wxSecretStore::GetDefault();
        if (id.empty() || !store.IsOk())
        {
            return false;
        }
        wxString username;
        wxSecretValue secret;
        if (!store.Load(ServiceFor(id), username, secret))
        {
            return false;
        }
        password = secret.GetAsString(wxConvUTF8);
        return true;
    }

    bool ConnectionStore::SavePassword(const wxString& id, const wxString& username, const wxString& password)
    {
        wxSecretStore store = wxSecretStore::GetDefault();
        if (id.empty() || !store.IsOk())
        {
            return false;
        }
        // The keychain wants some account name; VNC passwords have none.
        wxString account = username.empty() ? wxString("vnc") : username;
        if (!store.Save(ServiceFor(id), account, wxSecretValue(password)))
        {
            LOG_WARNING("Keychain", "Could not save the password for connection %s", id.utf8_string().c_str());
            return false;
        }
        return true;
    }

    void ConnectionStore::DeletePassword(const wxString& id)
    {
        wxSecretStore store = wxSecretStore::GetDefault();
        if (!id.empty() && store.IsOk())
        {
            store.Delete(ServiceFor(id));
        }
    }
} // namespace RmVncUI
