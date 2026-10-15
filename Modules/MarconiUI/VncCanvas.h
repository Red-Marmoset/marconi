// Copyright (c) 2026 Red Marmoset Studios Ltd.
// View of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer: actual size with
// scrollbars, or scaled to fit the window

#pragma once

#include "KeyMapper.h"

#include <MarconiCore/Rfb/Client.h>

#include <wx/bitmap.h>
#include <wx/scrolwin.h>

#include <functional>
#include <vector>

namespace MarconiUI
{
    enum class ScaleMode
    {
        Fit,    // scale the whole desktop into the window, keeping its aspect ratio
        Actual, // one desktop pixel per screen pixel, scroll if needed
    };

    class VncCanvas : public wxScrolledCanvas
    {
    public:
        VncCanvas(wxWindow* parent, Marconi::Rfb::Client& client);

        // UI thread only. Recreates the backing bitmap at the new desktop size.
        void SetDesktopSize(int width, int height);
        // UI thread only. Copies the changed areas from the framebuffer and repaints them.
        void UpdateRects(const std::vector<Marconi::Rfb::Rect>& rects);

        wxSize GetDesktopSize() const { return m_desktopSize; }

        void SetScaleMode(ScaleMode mode);
        ScaleMode GetScaleMode() const { return m_scaleMode; }
        // Current desktop-to-window scale factor (1 in Actual mode).
        double GetScale() const { return m_scale; }

        // View-only ignores local input (and releases anything held when switched on).
        void SetViewOnly(bool viewOnly);
        bool IsViewOnly() const { return m_viewOnly; }

        // Lifts every key and button the server thinks is held.
        void ReleaseInput();

        // UI thread only. Shows the server's cursor shape as our local cursor.
        void SetRemoteCursor(const Marconi::Rfb::CursorShape& cursor);

        // Called instead of sending F8, which opens the viewer's menu (as in other VNC viewers).
        void SetMenuKeyHandler(std::function<void()> handler) { m_menuKeyHandler = std::move(handler); }

    private:
        void UpdateLayout();
        void ApplyCursor();
        void CopyFromFramebuffer(const Marconi::Rfb::Rect& rect);
        void OnPaint(wxPaintEvent& event);
        void OnSize(wxSizeEvent& event);

        // Window (client) coordinates <-> desktop coordinates, for the current mode, scroll position and scale.
        wxPoint ToDesktop(const wxPoint& windowPoint) const;
        wxRect ToWindow(const wxRect& desktopRect) const;

        void SendPointer(const wxPoint& windowPoint);
        void OnMouse(wxMouseEvent& event);
        void OnMouseWheel(wxMouseEvent& event);
        void OnCaptureLost(wxMouseCaptureLostEvent& event);
        void OnKeyDown(wxKeyEvent& event);
        void OnKeyUp(wxKeyEvent& event);
        void OnChar(wxKeyEvent& event);
        void OnKillFocus(wxFocusEvent& event);

        Marconi::Rfb::Client& m_client;
        wxBitmap m_backing;
        wxSize m_desktopSize;

        ScaleMode m_scaleMode = ScaleMode::Fit;
        double m_scale = 1.0;
        wxPoint m_offset; // top-left of the scaled desktop inside the window (Fit mode letterboxing)

        KeyMapper m_keys;
        bool m_viewOnly = false;
        uint8_t m_buttons = 0;
        wxPoint m_lastPointer = wxPoint(-1, -1);
        int m_wheelVertical = 0;
        int m_wheelHorizontal = 0;
        Marconi::Rfb::CursorShape m_remoteCursor;
        bool m_hasRemoteCursor = false;
        double m_cursorScale = 0.0;
        std::function<void()> m_menuKeyHandler;
    };
} // namespace MarconiUI
