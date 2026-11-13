// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Shows a notification (a balloon on Windows, a banner on a Mac) when a viewer connects or disconnects

#pragma once

#include <MarconiServerCore/ConnectionNotifier.h>

#include <atomic>

namespace Marconi
{
    namespace ServerUI
    {
        // The server calls Notify from its own threads; the notification is made on the main (window) thread.
        class TrayNotifier : public Server::ConnectionNotifier
        {
        public:
            explicit TrayNotifier(bool enabled) : m_enabled(enabled) {}

            void Notify(const Server::ConnectionNotice& notice) override;

            void SetEnabled(bool enabled) { m_enabled = enabled; }
            bool IsEnabled() const { return m_enabled; }

        private:
            std::atomic<bool> m_enabled;
        };
    } // namespace ServerUI
} // namespace Marconi
