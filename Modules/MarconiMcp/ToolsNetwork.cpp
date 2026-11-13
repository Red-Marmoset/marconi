// Copyright (c) 2026 Red Marmoset Studios Ltd.
// MarconiTools: the computers on this network (what Marconi shows under "On this network"), for list_connections
// and for connecting to one by the name it announces

#include <MarconiMcp/ToolArguments.h>
#include <MarconiMcp/Tools.h>

#include <MarconiCore/Address.h>
#include <MarconiCore/Net/ProtocolChoice.h>

#include <algorithm>
#include <cctype>

namespace Marconi
{
    namespace Mcp
    {
        namespace
        {
            std::string Lower(std::string text)
            {
                std::transform(text.begin(), text.end(), text.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                return text;
            }

            // "Phone.local." and "Phone.local" are the same name.
            std::string HostName(const Net::DiscoveredService& service)
            {
                std::string host = service.hostName;
                if (!host.empty() && host.back() == '.')
                {
                    host.pop_back();
                }
                return host;
            }

            // Whether a saved connection already points at this computer.
            bool IsSaved(const Net::DiscoveredService& service, const std::vector<SavedComputer>& saved)
            {
                const std::string host = Lower(HostName(service));
                for (const SavedComputer& computer : saved)
                {
                    Address parsed;
                    std::string error;
                    if (!ParseAddress(computer.address, parsed, error))
                    {
                        continue;
                    }
                    const std::string savedHost = Lower(parsed.host);
                    if ((!host.empty() && savedHost == host) || (!service.ipv4.empty() && savedHost == service.ipv4))
                    {
                        return true;
                    }
                }
                return false;
            }

            const char* KindName(Protocol protocol)
            {
                switch (protocol)
                {
                case Protocol::Marconi:
                    return "Marconi";
                case Protocol::Vnc:
                    return "VNC";
                case Protocol::Rdp:
                    return "Remote Desktop";
                case Protocol::Ssh:
                    return "SSH";
                default:
                    return "unknown";
                }
            }

            bool Usable(Protocol protocol)
            {
                // The AI tools speak what QuickConnect speaks, less Remote Desktop for now.
                return Net::IsSupported(protocol) && protocol != Protocol::Rdp;
            }
        } // namespace

        const std::vector<Net::DiscoveredService>& MarconiTools::NetworkComputers()
        {
            const auto now = std::chrono::steady_clock::now();
            if (m_options.discover &&
                (!m_networkSearchedOnce || now - m_networkSearched > std::chrono::seconds(kNetworkCacheSeconds)))
            {
                m_network = m_options.discover();
                m_networkSearched = now;
                m_networkSearchedOnce = true;
            }
            return m_network;
        }

        nlohmann::json MarconiTools::NetworkList(const std::vector<SavedComputer>& saved)
        {
            nlohmann::json list = nlohmann::json::array();
            for (const Net::DiscoveredService& service : NetworkComputers())
            {
                if (IsSaved(service, saved))
                {
                    continue;
                }
                nlohmann::json entry = {{"name", service.instance},
                                        {"address", service.ipv4.empty() ? HostName(service) : service.ipv4},
                                        {"kind", KindName(service.protocol)}};
                if (!Usable(service.protocol))
                {
                    entry["usable"] = false;
                }
                list.push_back(entry);
            }
            return list;
        }

        bool MarconiTools::FindNetworkComputer(const std::string& name, std::string& address, std::string& foundName)
        {
            const std::string wanted = Lower(name);
            for (const Net::DiscoveredService& service : NetworkComputers())
            {
                if (Lower(service.instance) != wanted)
                {
                    continue;
                }
                if (!Usable(service.protocol))
                {
                    throw ArgumentError{"\"" + service.instance + "\" offers " + KindName(service.protocol) +
                                        ", which Marconi's AI tools can't use yet."};
                }
                Address parsed;
                parsed.host = service.ipv4.empty() ? HostName(service) : service.ipv4;
                parsed.port = service.port;
                parsed.protocol = service.protocol;
                address = FormatAddress(parsed);
                foundName = service.instance;
                return true;
            }
            return false;
        }
    } // namespace Mcp
} // namespace Marconi
