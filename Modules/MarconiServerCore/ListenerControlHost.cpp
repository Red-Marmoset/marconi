// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Answers the tray icon's control-pipe requests from a running Listener

#include "ListenerControlHost.h"

namespace Marconi
{
    namespace Server
    {
        ListenerControlHost::ListenerControlHost(Listener& listener, const Settings& settings,
                                                 uint32_t consoleSession, PasswordChanger changePassword,
                                                 DesktopNamer desktopName)
            : m_listener(listener), m_settings(settings), m_consoleSession(consoleSession),
              m_changePassword(std::move(changePassword)), m_desktopName(std::move(desktopName)),
              m_passwordSet(!settings.passwordHash.empty())
        {
        }

        Control::ServerStatus ListenerControlHost::GetStatus()
        {
            Control::ServerStatus status;
            status.serverVersion = m_settings.serverVersion;
            status.port = m_listener.GetPort();
            status.lockOnDisconnect = m_settings.lockOnDisconnect;
            status.consoleSession = m_consoleSession;
            status.fingerprint = m_listener.GetCertificateFingerprint();
            if (m_desktopName)
            {
                status.desktop = m_desktopName();
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                status.passwordSet = m_passwordSet;
            }
            ViewerSnapshot viewer;
            if (m_listener.GetViewer(viewer))
            {
                Control::ViewerInfo info;
                info.id = viewer.id;
                info.name = viewer.name;
                info.connectedSeconds = viewer.connectedSeconds;
                status.viewers.push_back(std::move(info));
            }
            return status;
        }

        Control::ResultCode ListenerControlHost::DisconnectViewer(uint32_t viewerId, std::string& text)
        {
            if (!m_listener.DisconnectViewer(viewerId))
            {
                text = "Nobody is connected.";
                return Control::ResultCode::NoSuchViewer;
            }
            return Control::ResultCode::Ok;
        }

        Control::ResultCode ListenerControlHost::SetPasswordHash(const std::string& passwordHash, std::string& text)
        {
            if (!m_changePassword)
            {
                text = "This server can't change its password while it runs.";
                return Control::ResultCode::Failed;
            }
            Control::ResultCode code = m_changePassword(passwordHash, text);
            if (code == Control::ResultCode::Ok)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_passwordSet = !passwordHash.empty();
            }
            return code;
        }
    } // namespace Server
} // namespace Marconi
