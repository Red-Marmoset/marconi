// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tells the person at this computer when a viewer connects or goes away

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        enum class ConnectionEvent
        {
            Connected,        // a viewer passed the login and can now see the screen
            Disconnected,     // it left (or was disconnected from this computer), after having connected
            IdleDisconnected, // it was ended because it had not been used for the set time
        };

        struct ConnectionNotice
        {
            ConnectionEvent event = ConnectionEvent::Connected;
            std::string viewerName; // the viewer's name from its Hello; may be empty
            std::string address;    // where it connected from, as text; may be empty if unknown
        };

        // The plain-words sentence for a notice, e.g. "Anna's laptop (192.168.1.20) connected to this computer."
        // Shared by the tray's notification and the console's line, so they say the same thing.
        inline std::string DescribeNotice(const ConnectionNotice& notice)
        {
            std::string who = notice.viewerName.empty() ? std::string("A viewer") : notice.viewerName;
            if (!notice.address.empty())
            {
                who += " (" + notice.address + ")";
            }
            switch (notice.event)
            {
            case ConnectionEvent::Connected:
                return who + " connected to this computer.";
            case ConnectionEvent::IdleDisconnected:
                return who + " was disconnected from this computer because it had not been used for a while.";
            case ConnectionEvent::Disconnected:
            default:
                return who + " disconnected from this computer.";
            }
        }

        // Where connection notices go. The server calls it from its own threads (never from a window's thread), so
        // an implementation that shows something on screen must hand the work over to the thread that owns the
        // screen. It must not block for long: the viewer's session waits for it.
        class ConnectionNotifier
        {
        public:
            virtual ~ConnectionNotifier() = default;
            virtual void Notify(const ConnectionNotice& notice) = 0;
        };

        // Keeps every notice, for tests.
        class RecordingNotifier : public ConnectionNotifier
        {
        public:
            void Notify(const ConnectionNotice& notice) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_notices.push_back(notice);
            }
            std::vector<ConnectionNotice> Notices() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_notices;
            }

        private:
            mutable std::mutex m_mutex;
            std::vector<ConnectionNotice> m_notices;
        };
    } // namespace Server
} // namespace Marconi
