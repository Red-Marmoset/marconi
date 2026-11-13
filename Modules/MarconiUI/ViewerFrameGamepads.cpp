// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The viewer window's game controller forwarding: the menu setting, and the timer that hands this computer's
// controllers to a Marconi Server while the window is in front

#include "ViewerFrame.h"

#include <MarconiCore/Wire/WireClient.h>

#include <chrono>

namespace MarconiUI
{
    void ViewerFrame::StartGamepads()
    {
        StopGamepads();
        if (!m_sendGamepads || !m_connected)
        {
            return;
        }
        if (m_wire == nullptr)
        {
            m_sendGamepads = false;
            SetStatusText("Only a Marconi Server can take game controllers.");
            return;
        }
        if (!m_wire->SupportsGamepads())
        {
            m_sendGamepads = false;
            SetStatusText("The other computer doesn't take game controllers.");
            return;
        }
        if (!m_gamepadSource)
        {
            std::string reason;
            m_gamepadSource = Marconi::Gamepad::CreateSystemGamepadSource(reason);
            if (!m_gamepadSource)
            {
                m_sendGamepads = false;
                SetStatusText(wxString::FromUTF8(reason));
                return;
            }
        }
        m_gamepadForwarder = std::make_unique<Marconi::Gamepad::GamepadForwarder>(*m_gamepadSource, *m_wire);
        m_gamepadTimer.Start(8);
    }

    void ViewerFrame::StopGamepads()
    {
        m_gamepadTimer.Stop();
        // Destroying the forwarder lets go of everything held down on the other computer, so it must happen while the
        // connection still exists.
        m_gamepadForwarder.reset();
    }

    void ViewerFrame::OnGamepadTick()
    {
        if (!m_gamepadForwarder)
        {
            return;
        }
        const bool active = m_sendGamepads && m_connected && m_active && !m_canvas->IsViewOnly();
        m_gamepadForwarder->Tick(std::chrono::steady_clock::now(), active);
        const std::string notice = m_gamepadForwarder->TakeNotice();
        if (!notice.empty())
        {
            SetStatusText(wxString::FromUTF8(notice));
        }
    }
} // namespace MarconiUI
