// Copyright (c) 2026 Red Marmoset Studios Ltd.
// See TrayTransportLink.h

#include "TrayTransportLink.h"

#include "ControlPipe.h"
#include "ControlSession.h"

namespace Marconi
{
    namespace Server
    {
        namespace Tray
        {
            namespace
            {
                class TransportLink : public Link
                {
                public:
                    explicit TransportLink(std::unique_ptr<Wire::Transport> transport)
                        : m_transport(std::move(transport)), m_client(*m_transport)
                    {
                    }

                    bool Handshake(const std::string& clientName, std::string& error, bool& versionMismatch)
                    {
                        const bool ok = m_client.Connect(clientName, error);
                        versionMismatch = m_client.WasVersionMismatch();
                        return ok;
                    }

                    bool GetStatus(Control::ServerStatus& status, std::string& error) override
                    {
                        return m_client.RefreshStatus(status, error);
                    }
                    Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) override
                    {
                        return m_client.DisconnectViewer(viewerId, text);
                    }
                    Control::ResultCode SetPasswordHash(const std::string& passwordHash, std::string& text) override
                    {
                        return m_client.SetPasswordHash(passwordHash, text);
                    }

                private:
                    std::unique_ptr<Wire::Transport> m_transport;
                    Control::ControlClient m_client;
                };
            } // namespace

            ConnectResult TransportConnector::Connect()
            {
                ConnectResult result;
                std::string error;
                std::unique_ptr<Wire::Transport> transport = m_factory(error);
                if (!transport)
                {
                    result.outcome = ConnectOutcome::Absent;
                    result.text = error;
                    return result;
                }
                std::unique_ptr<TransportLink> link = std::make_unique<TransportLink>(std::move(transport));
                bool versionMismatch = false;
                if (!link->Handshake(m_clientName, error, versionMismatch))
                {
                    result.outcome = versionMismatch ? ConnectOutcome::IncompatibleVersion : ConnectOutcome::Failed;
                    result.text = error;
                    return result;
                }
                result.outcome = ConnectOutcome::Connected;
                result.link = std::move(link);
                return result;
            }

            TransportFactory NamedPipeFactory()
            {
                return [](std::string& error) { return Control::ConnectToControlPipe(error); };
            }
        } // namespace Tray
    } // namespace Server
} // namespace Marconi
