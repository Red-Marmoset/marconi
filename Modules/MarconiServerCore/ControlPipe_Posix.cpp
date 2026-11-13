// Copyright (c) 2026 Red Marmoset Studios Ltd.
// No control pipe on macOS and Linux yet: the tray icon and the helper are one process there (Phase 8)

#include "ControlPipe.h"

namespace Marconi
{
    namespace Server
    {
        namespace Control
        {
            struct ControlPipeServer::Impl
            {
            };

            ControlPipeServer::ControlPipeServer(ControlHost&, const std::string&) : m_impl(new Impl)
            {
            }

            ControlPipeServer::~ControlPipeServer() = default;

            bool ControlPipeServer::Start(std::string& error)
            {
                error = "the control pipe is for Windows only";
                return false;
            }

            void ControlPipeServer::Stop()
            {
            }

            void ControlPipeServer::NotifyChanged()
            {
            }

            std::unique_ptr<Wire::Transport> ConnectToControlPipe(std::string& error)
            {
                error = "the control pipe is for Windows only";
                return nullptr;
            }
        } // namespace Control
    } // namespace Server
} // namespace Marconi
