// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Scrollable view of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer

#pragma once

#include "KeyMapper.h"

#include <RmVncCore/Rfb/Client.h>

#include <wx/bitmap.h>
#include <wx/scrolwin.h>

#include <vector>

namespace RmVncUI
{
    class VncCanvas : public wxScrolledCanvas
    {
    public:
        VncCanvas(wxWindow* parent, RmVnc::Rfb::Client& client);

        // UI thread only. Recreates the backing bitmap at the new desktop size.
        void SetDesktopSize(int width, int height);
        // UI thread only. Copies the changed areas from the framebuffer and repaints them.
        void UpdateRects(const std::vector<RmVnc::Rfb::Rect>& rects);

        wxSize GetDesktopSize() const { return m_desktopSize; }

        // View-only ignores local input (and releases anything held when switched on).
        void SetViewOnly(bool viewOnly);
        bool IsViewOnly() const { return m_viewOnly; }

        // Lifts every key and button the server thinks is held.
        void ReleaseInput();

    private:
        void CopyFromFramebuffer(const RmVnc::Rfb::Rect& rect);
        void OnPaint(wxPaintEvent& event);

        wxPoint ToDesktop(const wxPoint& windowPoint) const;
        void SendPointer(const wxPoint& windowPoint);
        void OnMouse(wxMouseEvent& event);
        void OnMouseWheel(wxMouseEvent& event);
        void OnCaptureLost(wxMouseCaptureLostEvent& event);
        void OnKeyDown(wxKeyEvent& event);
        void OnKeyUp(wxKeyEvent& event);
        void OnChar(wxKeyEvent& event);
        void OnKillFocus(wxFocusEvent& event);

        RmVnc::Rfb::Client& m_client;
        wxBitmap m_backing;
        wxSize m_desktopSize;

        KeyMapper m_keys;
        bool m_viewOnly = false;
        uint8_t m_buttons = 0;
        wxPoint m_lastPointer = wxPoint(-1, -1);
        int m_wheelVertical = 0;
        int m_wheelHorizontal = 0;
    };
} // namespace RmVncUI
