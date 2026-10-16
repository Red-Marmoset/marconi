// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Saved connections: settings in wxConfig, passwords in the OS keychain (wxSecretStore)

#include "ConnectionStore.h"

#include <Log/Log.h>

#include <wx/config.h>
#include <wx/secretstore.h>

#include <algorithm>
#include <climits>
#include <memory>

namespace MarconiUI
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
            return "marconi/" + id;
        }

        void CopyGroup(wxConfigBase& from, wxConfigBase& to, const wxString& path)
        {
            from.SetPath(path);
            std::vector<wxString> entries;
            std::vector<wxString> groups;
            wxString name;
            long cookie = 0;
            for (bool more = from.GetFirstEntry(name, cookie); more; more = from.GetNextEntry(name, cookie))
            {
                entries.push_back(name);
            }
            for (bool more = from.GetFirstGroup(name, cookie); more; more = from.GetNextGroup(name, cookie))
            {
                groups.push_back(name);
            }
            from.SetPath("/");

            for (const wxString& entry : entries)
            {
                wxString key = path + "/" + entry;
                switch (from.GetEntryType(key))
                {
                case wxConfigBase::Type_Integer:
                {
                    // Keep 32-bit values 32-bit (the registry reads booleans from DWORDs).
                    long long value = from.ReadLongLong(key, 0);
                    if (value >= LONG_MIN && value <= LONG_MAX)
                    {
                        to.Write(key, static_cast<long>(value));
                    }
                    else
                    {
                        to.Write(key, value);
                    }
                    break;
                }
                case wxConfigBase::Type_Boolean:
                    to.Write(key, from.ReadBool(key, false));
                    break;
                case wxConfigBase::Type_Float:
                    to.Write(key, from.ReadDouble(key, 0.0));
                    break;
                default:
                    to.Write(key, from.Read(key, wxString()));
                    break;
                }
            }
            for (const wxString& group : groups)
            {
                CopyGroup(from, to, path + "/" + group);
            }
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
        out.detected = config->Read(path + "/Detected", wxEmptyString);
        out.username = config->Read(path + "/Username", wxEmptyString);
        out.rememberPassword = config->ReadBool(path + "/RememberPassword", false);
        out.certificate = config->Read(path + "/Certificate", wxEmptyString);
        out.layout = config->Read(path + "/Layout", wxEmptyString);
        out.sshHostKey = config->Read(path + "/SshHostKey", wxEmptyString);
        out.dropFolder = config->Read(path + "/DropFolder", wxEmptyString);
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
        config->Write(path + "/Detected", connection.detected);
        config->Write(path + "/Username", connection.username);
        config->Write(path + "/RememberPassword", connection.rememberPassword);
        config->Write(path + "/Certificate", connection.certificate);
        config->Write(path + "/Layout", connection.layout);
        config->Write(path + "/SshHostKey", connection.sshHostKey);
        config->Write(path + "/DropFolder", connection.dropFolder);
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

    void ConnectionStore::MigrateFromRmvnc()
    {
        const wxString kMigrated = "/Migration/FromRmvnc";
        wxConfigBase* config = wxConfigBase::Get();
        if (config->ReadBool(kMigrated, false) || config->HasGroup(kRoot))
        {
            return;
        }
        config->Write(kMigrated, true);

        // The same kind of config wxConfigBase::Create() made for the app when it was called rmvnc.
        std::unique_ptr<wxConfigBase> old = std::make_unique<wxConfig>("rmvnc", "Red Marmoset Studios");
        if (!old->HasGroup(kRoot))
        {
            config->Flush();
            return;
        }
        CopyGroup(*old, *config, kRoot);
        config->Flush();

        wxSecretStore store = wxSecretStore::GetDefault();
        ConnectionStore connections;
        int count = 0;
        for (const SavedConnection& connection : connections.LoadAll())
        {
            ++count;
            wxString username;
            wxSecretValue secret;
            if (store.IsOk() && store.Load("rmvnc/" + connection.id, username, secret))
            {
                store.Save(ServiceFor(connection.id), username, secret);
            }
        }
        LOG_INFO("App", "Copied %d saved connections from rmvnc", count);
    }

    void ConnectionStore::DeletePassword(const wxString& id)
    {
        wxSecretStore store = wxSecretStore::GetDefault();
        if (!id.empty() && store.IsOk())
        {
            store.Delete(ServiceFor(id));
        }
    }
} // namespace MarconiUI
