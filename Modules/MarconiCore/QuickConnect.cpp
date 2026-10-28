// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Connecting the way the phone and tablet apps do: straight from the typed address, without the probe.

#include <MarconiCore/QuickConnect.h>

namespace Marconi
{
    namespace
    {
        Address AddressFor(const QuickConnectOptions& options, std::string& error)
        {
            Address address;
            if (!ParseAddress(options.address, address, error))
            {
                return {};
            }
            if (!options.username.empty())
            {
                address.username = options.username;
            }
            return address;
        }
    } // namespace

    Rfb::ConnectionOptions QuickConnectVncOptions(const Address& address, const QuickConnectOptions& options)
    {
        Rfb::ConnectionOptions connection;
        connection.host = address.host;
        connection.credentials.username = address.username;
        connection.credentials.password = options.password;
        connection.trustedCertificateSha256 = options.trustedCertificateSha256;
        if (address.protocol == Protocol::Ssh)
        {
            // Through the computer's SSH server (on the address's port, if any) to the VNC server on its usual port.
            connection.sshTunnel = true;
            connection.sshPort = address.port != 0 ? address.port : DefaultPort(Protocol::Ssh);
            connection.port = DefaultPort(Protocol::Vnc);
        }
        else
        {
            connection.port = EffectivePort(address);
        }
        return connection;
    }

    Wire::ClientOptions QuickConnectMarconiOptions(const Address& address, const QuickConnectOptions& options)
    {
        Wire::ClientOptions connection;
        connection.host = address.host;
        connection.port = EffectivePort(address);
        connection.password = options.password;
        connection.clientName = options.clientName;
        connection.trustedCertificateSha256 = options.trustedCertificateSha256;
        return connection;
    }

    std::unique_ptr<RemoteSession> QuickConnect(const QuickConnectOptions& options, SessionListener& listener,
                                                std::string& error)
    {
        error.clear();
        Address address = AddressFor(options, error);
        if (!error.empty())
        {
            return nullptr;
        }
        switch (address.protocol)
        {
            case Protocol::Rdp:
                error = "Remote Desktop connections aren't in this app yet.";
                return nullptr;

            case Protocol::Marconi:
            {
                auto client = std::make_unique<Wire::Client>(listener);
                client->Connect(QuickConnectMarconiOptions(address, options));
                return client;
            }

            case Protocol::Ssh:
            case Protocol::Vnc:
            case Protocol::Auto:
            default:
            {
                // Auto is VNC here: these apps don't probe for the other protocols.
                auto client = std::make_unique<Rfb::Client>(listener);
                client->Connect(QuickConnectVncOptions(address, options));
                return client;
            }
        }
    }
} // namespace Marconi
