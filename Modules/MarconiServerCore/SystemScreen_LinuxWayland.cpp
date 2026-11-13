// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux screen capture on a Wayland desktop: the picture comes from the desktop portal's PipeWire stream (see
// WaylandPortal.h), with the same tile-by-tile change tracking as the X11 and macOS capture.

#include "WaylandSupport.h"

#include "CaptureStatus.h"
#include "DirtyTracker.h"
#include "SystemScreen.h"

#include <cstdlib>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::mutex g_sessionMutex;
            std::weak_ptr<WaylandPortalSession> g_session;
            WaylandPortalOptions g_options;

            class WaylandScreen : public ScreenSource
            {
            public:
                explicit WaylandScreen(std::shared_ptr<WaylandPortalSession> session) : m_session(std::move(session)) {}

                // Until the first picture arrives (the person hasn't answered yet) the layout is a placeholder; the
                // session sends the real one when the size changes.
                std::vector<Wire::Display> GetDisplays() override
                {
                    Wire::Display display;
                    display.id = 1;
                    int32_t width = 0;
                    int32_t height = 0;
                    if (m_session->GetCapture().GetSize(width, height))
                    {
                        display.width = width;
                        display.height = height;
                    }
                    else
                    {
                        display.width = 1280;
                        display.height = 720;
                    }
                    return {display};
                }

                bool Grab(Capture& out) override
                {
                    int32_t width = 0;
                    int32_t height = 0;
                    uint64_t before = m_sequence;
                    if (!m_session->GetCapture().GetLatest(m_pixels, width, height, m_sequence))
                    {
                        return false;
                    }
                    out.width = width;
                    out.height = height;
                    out.pixels = m_pixels;
                    if (m_sequence != before)
                    {
                        m_tracker.Update(out);
                    }
                    else
                    {
                        out.dirty.clear();
                    }
                    return true;
                }

                bool GetCursor(Cursor& out) override
                {
                    return m_session->CursorIsSeparate() && m_session->GetCapture().GetCursor(out);
                }

                bool GetCursorPosition(int32_t& x, int32_t& y) override
                {
                    return m_session->CursorIsSeparate() && m_session->GetCapture().GetCursorPosition(x, y);
                }

            private:
                std::shared_ptr<WaylandPortalSession> m_session;
                std::vector<uint32_t> m_pixels;
                uint64_t m_sequence = 0;
                DirtyTracker m_tracker;
            };
        } // namespace

        bool ShouldUseWaylandPortal()
        {
            const char* forced = std::getenv("MARCONI_WAYLAND_PORTAL");
            if (forced != nullptr && *forced != '\0')
            {
                return std::string(forced) != "0";
            }
            const char* type = std::getenv("XDG_SESSION_TYPE");
            if (type != nullptr && std::string(type) == "wayland")
            {
                return true;
            }
            const char* display = std::getenv("DISPLAY");
            const char* wayland = std::getenv("WAYLAND_DISPLAY");
            return (display == nullptr || *display == '\0') && wayland != nullptr && *wayland != '\0';
        }

        void SetWaylandPortalOptions(const WaylandPortalOptions& options)
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            g_options = options;
        }

        std::shared_ptr<WaylandPortalSession> FindWaylandPortal()
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            return g_session.lock();
        }

        std::shared_ptr<WaylandPortalSession> AcquireWaylandPortal()
        {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            std::shared_ptr<WaylandPortalSession> session = g_session.lock();
            if (session == nullptr)
            {
                session = WaylandPortalSession::Start(g_options);
                g_session = session;
                std::weak_ptr<WaylandPortalSession> weak = session;
                SetCaptureStatusProvider([weak]() {
                    std::shared_ptr<WaylandPortalSession> alive = weak.lock();
                    return alive != nullptr ? alive->GetStatusText() : std::string();
                });
            }
            return session;
        }

        std::unique_ptr<ScreenSource> CreateWaylandScreen(std::shared_ptr<WaylandPortalSession> session)
        {
            return std::make_unique<WaylandScreen>(std::move(session));
        }

        std::unique_ptr<ScreenSource> CreateWaylandSystemScreen(std::string& error)
        {
            std::string busError;
            if (!DBusLink::IsAvailable(busError))
            {
                error = "This is a Wayland desktop, and sharing it needs the D-Bus library. " + busError;
                return nullptr;
            }
            std::shared_ptr<WaylandPortalSession> session = AcquireWaylandPortal();
            // Give a remembered answer a moment to come back, so the common case reports no problem at all.
            session->WaitUntilAnsweredOrAsking(3000);
            WaylandPortalSession::State state = session->GetState();
            if (state == WaylandPortalSession::State::Declined || state == WaylandPortalSession::State::Failed)
            {
                // The desktop already said no (or can't do it): report that, so the server serves its test picture
                // and the tray explains. Letting go of the session here means "Check again" asks afresh.
                error = session->GetStatusText();
                return nullptr;
            }
            // Still asking the person, or sharing: the picture appears once they allow it, and
            // GetCaptureStatus says what to do until then.
            return CreateWaylandScreen(std::move(session));
        }
    } // namespace Server
} // namespace Marconi
