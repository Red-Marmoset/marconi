// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The tray icon's mode state machine and the words it shows; see TrayMode.h

#include "TrayMode.h"

namespace Marconi
{
    namespace Server
    {
        namespace Tray
        {
            namespace
            {
                std::string DescribeDuration(uint32_t seconds)
                {
                    if (seconds < 60)
                    {
                        return "less than a minute";
                    }
                    const uint32_t minutes = seconds / 60;
                    if (minutes < 60)
                    {
                        return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
                    }
                    const uint32_t hours = minutes / 60;
                    std::string text = std::to_string(hours) + (hours == 1 ? " hour" : " hours");
                    if (minutes % 60 != 0)
                    {
                        text += " " + std::to_string(minutes % 60) + " min";
                    }
                    return text;
                }

                const char* const kUnreachable = "Marconi Server isn't reachable right now. Try again in a few seconds.";
            } // namespace

            const char* ModeName(Mode mode)
            {
                switch (mode)
                {
                case Mode::Unknown:
                    return "unknown";
                case Mode::OwnServer:
                    return "own-server";
                case Mode::StatusOnly:
                    return "status-only";
                case Mode::ServiceUnreadable:
                    return "service-unreadable";
                }
                return "unknown";
            }

            TrayMode::TrayMode(Connector& connector, Clock clock, Timing timing)
                : m_connector(connector), m_clock(std::move(clock)), m_timing(timing)
            {
            }

            void TrayMode::Tick()
            {
                const int64_t now = m_clock();
                if (m_polledOnce && now < m_nextPollAt)
                {
                    return;
                }
                m_polledOnce = true;
                m_nextPollAt = now + m_timing.pollIntervalMs;
                Poll();
            }

            void TrayMode::SetMode(Mode mode)
            {
                if (m_snapshot.mode == mode)
                {
                    return;
                }
                const Mode from = m_snapshot.mode;
                m_snapshot.mode = mode;
                if (mode != Mode::StatusOnly)
                {
                    m_snapshot.status = Control::ServerStatus();
                }
                if (mode != Mode::ServiceUnreadable)
                {
                    m_snapshot.notice.clear();
                }
                if (m_onModeChanged)
                {
                    m_onModeChanged(from, mode);
                }
                StatusChanged();
            }

            void TrayMode::StatusChanged()
            {
                if (m_onStatusChanged)
                {
                    m_onStatusChanged();
                }
            }

            void TrayMode::LinkBroke()
            {
                m_link.reset();
            }

            void TrayMode::Poll()
            {
                // An open link just asks again; only when that fails does the tray icon try to reconnect, and one
                // failed poll that reconnects cleanly is not counted at all.
                if (m_link)
                {
                    Control::ServerStatus status;
                    std::string error;
                    if (m_link->GetStatus(status, error))
                    {
                        const bool changed = status != m_snapshot.status || m_snapshot.failedPolls != 0;
                        m_snapshot.status = status;
                        m_snapshot.failedPolls = 0;
                        if (changed)
                        {
                            StatusChanged();
                        }
                        return;
                    }
                    LinkBroke();
                }

                ConnectResult result = m_connector.Connect();
                if (result.outcome == ConnectOutcome::Connected && result.link)
                {
                    Control::ServerStatus status;
                    std::string error;
                    if (result.link->GetStatus(status, error))
                    {
                        m_link = std::move(result.link);
                        m_snapshot.failedPolls = 0;
                        const bool wasStatusOnly = m_snapshot.mode == Mode::StatusOnly;
                        const bool changed = status != m_snapshot.status;
                        m_snapshot.status = status;
                        SetMode(Mode::StatusOnly);
                        if (wasStatusOnly && changed)
                        {
                            StatusChanged();
                        }
                        return;
                    }
                    result.outcome = ConnectOutcome::Failed;
                    result.text = error;
                }

                switch (result.outcome)
                {
                case ConnectOutcome::IncompatibleVersion:
                    m_snapshot.failedPolls = 0;
                    m_snapshot.notice =
                        "Marconi Server is running on this computer, but it is a different version from this tray "
                        "icon, so its status can't be shown. Update Marconi Server so the two match.";
                    if (m_snapshot.mode == Mode::ServiceUnreadable)
                    {
                        StatusChanged();
                    }
                    SetMode(Mode::ServiceUnreadable);
                    return;
                case ConnectOutcome::Failed:
                    if (m_snapshot.mode == Mode::StatusOnly)
                    {
                        break; // counted below like a missing helper
                    }
                    m_snapshot.failedPolls = 0;
                    m_snapshot.notice = "Marconi Server is running on this computer but did not answer properly";
                    if (!result.text.empty())
                    {
                        m_snapshot.notice += " (" + result.text + ")";
                    }
                    m_snapshot.notice += ". The status can't be shown.";
                    if (m_snapshot.mode == Mode::ServiceUnreadable)
                    {
                        StatusChanged();
                    }
                    SetMode(Mode::ServiceUnreadable);
                    return;
                default:
                    break;
                }

                // Nothing (usable) answered.
                if (m_snapshot.mode == Mode::StatusOnly || m_snapshot.mode == Mode::ServiceUnreadable)
                {
                    ++m_snapshot.failedPolls;
                    if (m_snapshot.failedPolls >= m_timing.failuresBeforeOwnServer)
                    {
                        m_snapshot.failedPolls = 0;
                        SetMode(Mode::OwnServer);
                    }
                    else
                    {
                        StatusChanged(); // the menu says contact was lost
                    }
                    return;
                }
                SetMode(Mode::OwnServer);
            }

            ActionResult TrayMode::DisconnectViewer(uint32_t viewerId)
            {
                if (m_snapshot.mode != Mode::StatusOnly || !m_link)
                {
                    return {false, kUnreachable};
                }
                std::string text;
                Control::ResultCode code = m_link->DisconnectViewer(viewerId, text);
                ActionResult result = DescribeDisconnectResult(code, text);
                Poll(); // show the new state straight away
                return result;
            }

            ActionResult TrayMode::SetPasswordHash(const std::string& passwordHash)
            {
                if (m_snapshot.mode != Mode::StatusOnly || !m_link)
                {
                    return {false, std::string("The password was not changed. ") + kUnreachable};
                }
                std::string text;
                Control::ResultCode code = m_link->SetPasswordHash(passwordHash, text);
                ActionResult result = DescribePasswordResult(code, text);
                Poll();
                return result;
            }

            ActionResult DescribePasswordResult(Control::ResultCode code, const std::string& text)
            {
                switch (code)
                {
                case Control::ResultCode::Ok:
                    return {true, "The password was changed."};
                case Control::ResultCode::NotAdministrator:
                    return {false,
                            "Only an administrator can change the password of the Marconi Server that runs as a "
                            "Windows service. Start the Marconi Server tray icon as an administrator (right-click "
                            "it and choose Run as administrator) and try again. The password was not changed."};
                case Control::ResultCode::Failed:
                    return {false, "The password was not changed: " +
                                       (text.empty() ? std::string("the server could not save it.") : text)};
                default:
                    return {false, "The password was not changed: the server did not accept the request." +
                                       (text.empty() ? std::string() : " " + text)};
                }
            }

            ActionResult DescribeDisconnectResult(Control::ResultCode code, const std::string& text)
            {
                switch (code)
                {
                case Control::ResultCode::Ok:
                    return {true, ""};
                case Control::ResultCode::NoSuchViewer:
                    return {false, "That viewer has already gone."};
                default:
                    return {false, "The viewer could not be disconnected" +
                                       (text.empty() ? std::string(".") : ": " + text)};
                }
            }

            std::vector<MenuItem> StatusOnlyMenu(const Snapshot& snapshot)
            {
                std::vector<MenuItem> items;
                auto info = [&](const std::string& label) {
                    MenuItem item;
                    item.kind = ItemKind::Info;
                    item.label = label;
                    items.push_back(item);
                };
                auto plain = [&](ItemKind kind, const std::string& label, uint32_t viewerId = 0) {
                    MenuItem item;
                    item.kind = kind;
                    item.label = label;
                    item.viewerId = viewerId;
                    items.push_back(item);
                };

                if (snapshot.mode == Mode::ServiceUnreadable)
                {
                    info(snapshot.notice);
                    plain(ItemKind::Separator, "-");
                    plain(ItemKind::Quit, "Quit tray icon");
                    return items;
                }

                const Control::ServerStatus& status = snapshot.status;
                if (snapshot.failedPolls > 0)
                {
                    info("Lost contact with Marconi Server, trying again...");
                }
                info("Marconi Server is running on this computer");
                if (status.viewers.empty())
                {
                    info("Nobody is connected");
                }
                for (const Control::ViewerInfo& viewer : status.viewers)
                {
                    info("Connected: " + (viewer.name.empty() ? std::string("a viewer") : viewer.name) + " (" +
                         DescribeDuration(viewer.connectedSeconds) + ")");
                }
                info("Port " + std::to_string(status.port));
                info(status.passwordSet ? "Password required" : "No password set: viewers can't connect");
                if (!status.fingerprint.empty())
                {
                    info("Fingerprint " + status.fingerprint.substr(0, 16) + "...");
                    plain(ItemKind::CopyFingerprint, "Copy full fingerprint");
                }

                plain(ItemKind::Separator, "-");
                for (const Control::ViewerInfo& viewer : status.viewers)
                {
                    plain(ItemKind::DisconnectViewer,
                          status.viewers.size() == 1 ? std::string("Disconnect viewer") : "Disconnect " + viewer.name,
                          viewer.id);
                }
                plain(ItemKind::SetPassword, status.passwordSet ? "Change password..." : "Set password...");
                plain(ItemKind::Separator, "-");
                plain(ItemKind::Quit, "Quit tray icon (the server keeps running)");
                return items;
            }
        } // namespace Tray
    } // namespace Server
} // namespace Marconi
