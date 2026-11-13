// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: sharing the screen and controlling the computer on a Wayland desktop. A Wayland desktop doesn't let a
// program read the screen or press keys on its own; it offers the "desktop portal" (xdg-desktop-portal), which asks
// the person once and then gives the program a PipeWire video stream (ScreenCast) and keyboard/mouse control
// (RemoteDesktop). This class runs that conversation on its own thread so the server never waits on the person, and
// then holds the stream and the control calls.

#pragma once

#include "DBusLite.h"
#include "PipeWireCapture.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        struct WaylandPortalOptions
        {
            std::string busAddress;     // empty = this session's bus
            std::string tokenPath;      // where the "remember my answer" token is kept; empty = the data folder
            int approvalTimeoutMs = 600000; // how long to wait for the person to answer the desktop's question
            bool wantControl = true;    // ask for keyboard and mouse control too
        };

        class WaylandPortalSession
        {
        public:
            enum class State
            {
                Starting,            // talking to the desktop
                WaitingForApproval,  // the desktop is asking the person
                Ready,               // sharing
                Declined,            // the person said no (or closed the question)
                Failed,              // no portal, or it failed
                Ended                // the desktop stopped the sharing later
            };

            // Starts the conversation in the background and returns at once.
            static std::shared_ptr<WaylandPortalSession> Start(const WaylandPortalOptions& options);

            ~WaylandPortalSession();
            WaylandPortalSession(const WaylandPortalSession&) = delete;
            WaylandPortalSession& operator=(const WaylandPortalSession&) = delete;

            State GetState() const;

            // What the person at this computer needs to know, in plain words; empty when all is well (sharing, with
            // control). Also set when only control is missing.
            std::string GetStatusText() const;

            // Waits until the state is no longer Starting or WaitingForApproval, or the time is up. True if settled.
            bool WaitUntilSettled(int timeoutMs) const;

            // Waits until the desktop has either answered or started asking the person (state is not Starting).
            bool WaitUntilAnsweredOrAsking(int timeoutMs) const;

            // True once keyboard and mouse control was granted.
            bool HasControl() const;

            // The video stream (valid once Ready). Frames arrive on PipeWire's own thread.
            PipeWireCapture& GetCapture() { return m_capture; }

            // Whether the pointer comes as separate data (so the viewer draws it) or is painted into the picture.
            bool CursorIsSeparate() const { return m_cursorSeparate.load(); }

            // The shared area in the desktop's own coordinates (its size may differ from the picture's size on a
            // scaled display; control is mapped through it).
            void GetStreamGeometry(int32_t& x, int32_t& y, int32_t& width, int32_t& height) const;

            // Control calls, safe from any thread. They do nothing (and return false) when control wasn't granted.
            bool PointerMotionAbsolute(double x, double y, int32_t pictureWidth, int32_t pictureHeight);
            bool PointerButton(int32_t evdevButton, bool down);
            bool PointerAxisSteps(bool horizontal, int32_t steps);
            bool Key(uint32_t keysym, bool down);

        private:
            WaylandPortalSession() = default;
            void Run(WaylandPortalOptions options);
            bool Negotiate(const WaylandPortalOptions& options);
            void Monitor();
            void SetState(State state, const std::string& text);

            // One portal call that answers later through a Response signal. Fills `results` on success; `code` is
            // the portal's response code (0 ok, 1 cancelled by the person, 2 other).
            bool CallWithResponse(const std::string& interfaceName, const std::string& method,
                                  std::vector<DBusValue> args, DBusValue& options, DBusValue& results,
                                  uint32_t& code, int timeoutMs, std::string& error);

            std::unique_ptr<DBusLink> m_link;
            PipeWireCapture m_capture;
            std::thread m_thread;
            std::atomic<bool> m_stop{false};

            mutable std::mutex m_mutex;
            State m_state = State::Starting;
            std::string m_status;
            bool m_control = false;
            std::string m_sessionPath;
            uint32_t m_streamNode = 0;
            int32_t m_streamX = 0;
            int32_t m_streamY = 0;
            int32_t m_streamWidth = 0;
            int32_t m_streamHeight = 0;
            std::atomic<bool> m_cursorSeparate{false};
            int m_counter = 0;
            std::string m_senderToken;
            std::string m_tokenPath;
        };
    } // namespace Server
} // namespace Marconi
