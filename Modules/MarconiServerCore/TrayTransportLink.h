// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Connects the tray icon's state machine (TrayMode) to a real control connection: the Windows named pipe, a Unix
// socket (developer option and tests), or any other Wire::Transport. The messages are ControlClient's.

#pragma once

#include "TrayMode.h"

#include <MarconiCore/Wire/WireChannel.h>

#include <functional>
#include <memory>
#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace Tray
        {
            // Opens the transport, or returns null with `error` when nothing is there to connect to.
            using TransportFactory = std::function<std::unique_ptr<Wire::Transport>(std::string& error)>;

            class TransportConnector : public Connector
            {
            public:
                TransportConnector(TransportFactory factory, std::string clientName)
                    : m_factory(std::move(factory)), m_clientName(std::move(clientName))
                {
                }

                ConnectResult Connect() override;

            private:
                TransportFactory m_factory;
                std::string m_clientName;
            };

            // The Windows named pipe the service's helper listens on (ControlPipe.h). On other platforms there is
            // no such pipe, so it always reports Absent.
            TransportFactory NamedPipeFactory();
        } // namespace Tray
    } // namespace Server
} // namespace Marconi
