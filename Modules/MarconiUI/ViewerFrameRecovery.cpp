// Copyright (c) 2026 Red Marmoset Studios Ltd.
// ViewerFrame: getting the picture back when the server stops sending one, and retrying a lost connection

#include "ViewerFrame.h"

#include <MarconiCore/Reconnect.h>

#include <Log/Log.h>

#include <chrono>

namespace MarconiUI
{
    void ViewerFrame::LogTraffic()
    {
        // Shows whether the server keeps sending pictures, e.g. when the view freezes.
        Marconi::Rfb::TrafficStats stats = m_client.TakeTrafficStats();
        LOG_INFO("Viewer", "Last 10s: %d updates with %d rectangles, %d requests, %d pointer and %d key events sent",
                 stats.updates, stats.rectangles, stats.requests, stats.pointerEvents, stats.keyEvents);
    }

    void ViewerFrame::WaitForPicture()
    {
        // After connecting or a resize, until the server sends actual pixels.
        m_waitingForPicture = true;
        m_pictureRetries = 0;
        m_pictureTimer.Start(2000);
        UpdateStatus();
    }

    void ViewerFrame::CheckForPicture()
    {
        // macOS sometimes sends nothing but size changes when it switches display modes (e.g. at the lock screen)
        // or right after a previous session ended. Ask for the whole desktop again; if that doesn't help, reconnect,
        // since a fresh connection always gets a full picture.
        constexpr int kMaxRetries = 4;
        constexpr int kMaxAutoReconnects = 3;
        if (!m_connected || !m_waitingForPicture || !m_options.recovery)
        {
            m_pictureTimer.Stop();
            return;
        }
        if (m_pictureRetries < kMaxRetries)
        {
            ++m_pictureRetries;
            LOG_INFO("Viewer", "No picture from the server yet, asking for the whole desktop again (%d/%d)",
                     m_pictureRetries, kMaxRetries);
            m_client.RequestFullUpdate();
            return;
        }
        // Input sent during the reconnect would be lost (e.g. a password typed blind at the lock screen), and it
        // still reaches the server while the picture is stuck. Wait for a pause before reconnecting.
        constexpr std::chrono::seconds kQuietBeforeReconnect(3);
        if (m_client.GetTimeSinceInput() < kQuietBeforeReconnect)
        {
            LOG_INFO("Viewer", "Still no picture, but the user is typing or using the mouse; reconnecting later");
            m_client.RequestFullUpdate();
            return;
        }
        m_pictureTimer.Stop();
        if (m_autoReconnects >= kMaxAutoReconnects)
        {
            LOG_WARNING("Viewer", "Still no picture after %d reconnects; giving up", m_autoReconnects);
            SetStatusText("The server isn't sending a picture. Try Refresh, or reconnect later.");
            return;
        }
        ++m_autoReconnects;
        LOG_INFO("Viewer", "Still no picture; reconnecting (%d/%d)", m_autoReconnects, kMaxAutoReconnects);
        SetStatusText("The server stopped sending a picture; reconnecting...");
        m_autoReconnecting = true;
        m_client.Disconnect(); // OnDisconnected follows; HandleDisconnected reconnects
    }

    void ViewerFrame::EnterWakeState()
    {
        // Like a lost connection, but the picture (if any) isn't stale and the headline says what's happening.
        LOG_INFO("Viewer", "Sent a wake-up call; trying to connect for up to %d seconds", Marconi::Reconnect::kGiveUpSeconds);
        m_lostHeadline = "Sent a wake-up call, waiting for it to start";
        m_reconnecting = true;
        m_retryUnderWay = false;
        m_retryAttempt = 2; // skip the 2 and 4 second waits: a computer takes a while to start
        m_lostAt = std::chrono::steady_clock::now();
        m_aui.GetPane("ConnectionLost").Show();
        m_aui.Update();
        ScheduleRetry("");
    }

    void ViewerFrame::EnterLostState(const std::string& error)
    {
        LOG_INFO("Viewer", "Connection lost; retrying for up to %d seconds", Marconi::Reconnect::kGiveUpSeconds);
        m_lostHeadline = "Connection lost";
        m_reconnecting = true;
        m_retryUnderWay = false;
        m_retryAttempt = 0;
        m_lostAt = std::chrono::steady_clock::now();
        m_canvas->SetStale(true);
        m_aui.GetPane("ConnectionLost").Show();
        m_aui.Update();
        ScheduleRetry(error);
    }

    void ViewerFrame::ScheduleRetry(const std::string& error)
    {
        m_lostError = error;
        auto now = std::chrono::steady_clock::now();
        int sinceLost = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - m_lostAt).count());
        wxString reason = wxString::FromUTF8(error);
        if (Marconi::Reconnect::ShouldGiveUp(sinceLost))
        {
            m_retryTimer.Stop();
            m_lostBar->SetMessage("Couldn't reconnect in " + wxString::Format("%d", Marconi::Reconnect::kGiveUpSeconds / 60) +
                                  " minutes (" + reason + ").");
            m_lostBar->EnableRetry(true);
            SetStatusText("Connection lost: " + reason);
            return;
        }
        ++m_retryAttempt;
        m_retryDue = now + std::chrono::seconds(Marconi::Reconnect::DelaySeconds(m_retryAttempt));
        m_lostBar->EnableRetry(true);
        m_retryTimer.Start(1000);
        OnRetryTick();
    }

    void ViewerFrame::OnRetryTick()
    {
        if (!m_reconnecting || m_retryUnderWay)
        {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= m_retryDue)
        {
            RetryNow();
            return;
        }
        int seconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(m_retryDue - now).count()) + 1;
        wxString reason = wxString::FromUTF8(m_lostError);
        wxString headline = m_lostHeadline + (reason.empty() ? wxString() : " (" + reason + ")");
        m_lostBar->SetMessage(headline + ". Retrying in " + wxString::Format("%d", seconds) + " s" +
                              wxString::FromUTF8("\xE2\x80\xA6"));
        SetStatusText(headline);
    }

    void ViewerFrame::RetryNow()
    {
        if (!m_reconnecting || m_retryUnderWay)
        {
            return;
        }
        m_retryTimer.Stop();
        m_retryUnderWay = true;
        m_lostBar->EnableRetry(false);
        m_lostBar->SetMessage("Reconnecting to " + GetServerDisplayName() + wxString::FromUTF8("\xE2\x80\xA6"));
        ConnectVnc(m_port);
    }

    void ViewerFrame::LeaveLostState()
    {
        m_reconnecting = false;
        m_retryUnderWay = false;
        m_retryTimer.Stop();
        m_canvas->SetStale(false);
        m_aui.GetPane("ConnectionLost").Hide();
        m_aui.Update();
    }
} // namespace MarconiUI
