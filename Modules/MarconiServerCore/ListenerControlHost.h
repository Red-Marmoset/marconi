// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Answers the tray icon's control-pipe requests from a running Listener: who is connected, disconnecting them, and
// the status the tray icon shows

#pragma once

#include "ControlSession.h"
#include "Listener.h"
#include "Settings.h"

#include <functional>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        class ListenerControlHost : public Control::ControlHost
        {
        public:
            // Stores a new password hash somewhere lasting and makes logins use it (the helper restarts its
            // listener or updates its Auth). Returns Ok or Failed with a plain reason in `text`.
            using PasswordChanger = std::function<Control::ResultCode(const std::string& hash, std::string& text)>;
            // The name of the desktop the helper is looking at ("Default", "Winlogon"); may be empty.
            using DesktopNamer = std::function<std::string()>;

            ListenerControlHost(Listener& listener, const Settings& settings, uint32_t consoleSession,
                                PasswordChanger changePassword, DesktopNamer desktopName = DesktopNamer());

            Control::ServerStatus GetStatus() override;
            Control::ResultCode DisconnectViewer(uint32_t viewerId, std::string& text) override;
            Control::ResultCode SetPasswordHash(const std::string& passwordHash, std::string& text) override;

        private:
            Listener& m_listener;
            Settings m_settings;
            uint32_t m_consoleSession;
            PasswordChanger m_changePassword;
            DesktopNamer m_desktopName;
            std::mutex m_mutex;
            bool m_passwordSet;
        };
    } // namespace Server
} // namespace Marconi
