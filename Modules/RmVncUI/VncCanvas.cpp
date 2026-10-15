// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Scrollable view of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer

#include "VncCanvas.h"

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

#include <algorithm>
#include <cstdlib>

namespace RmVncUI
{
    namespace
    {
        const wxColour kOutsideColour(48, 48, 48);
    }

    VncCanvas::VncCanvas(wxWindow* parent, RmVnc::Rfb::Client& client)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          m_client(client),
          m_keys([&client](uint32_t keysym, bool down) { client.SendKeyEvent(keysym, down); })
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(16, 16);
        Bind(wxEVT_PAINT, &VncCanvas::OnPaint, this);

        for (const wxEventTypeTag<wxMouseEvent>& type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP,
                                 wxEVT_MIDDLE_DCLICK, wxEVT_RIGHT_DOWN, wxEVT_RIGHT_UP, wxEVT_RIGHT_DCLICK, wxEVT_MOTION})
        {
            Bind(type, &VncCanvas::OnMouse, this);
        }
        // Binding the wheel here also stops wxScrolledCanvas using it for our scrollbars: it drives the remote desktop.
        Bind(wxEVT_MOUSEWHEEL, &VncCanvas::OnMouseWheel, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &VncCanvas::OnCaptureLost, this);
        Bind(wxEVT_KEY_DOWN, &VncCanvas::OnKeyDown, this);
        Bind(wxEVT_KEY_UP, &VncCanvas::OnKeyUp, this);
        Bind(wxEVT_CHAR, &VncCanvas::OnChar, this);
        Bind(wxEVT_KILL_FOCUS, &VncCanvas::OnKillFocus, this);
    }

    void VncCanvas::SetViewOnly(bool viewOnly)
    {
        if (viewOnly && !m_viewOnly)
        {
            ReleaseInput();
        }
        m_viewOnly = viewOnly;
    }

    void VncCanvas::ReleaseInput()
    {
        m_keys.ReleaseAll();
        if (m_buttons != 0 && m_lastPointer.x >= 0)
        {
            m_client.SendPointerEvent(m_lastPointer.x, m_lastPointer.y, 0);
        }
        m_buttons = 0;
        if (HasCapture())
        {
            ReleaseMouse();
        }
    }

    wxPoint VncCanvas::ToDesktop(const wxPoint& windowPoint) const
    {
        wxPoint point = CalcUnscrolledPosition(windowPoint);
        point.x = std::clamp(point.x, 0, std::max(m_desktopSize.x - 1, 0));
        point.y = std::clamp(point.y, 0, std::max(m_desktopSize.y - 1, 0));
        return point;
    }

    void VncCanvas::SendPointer(const wxPoint& windowPoint)
    {
        m_lastPointer = ToDesktop(windowPoint);
        m_client.SendPointerEvent(m_lastPointer.x, m_lastPointer.y, m_buttons);
    }

    void VncCanvas::OnMouse(wxMouseEvent& event)
    {
        if (event.ButtonDown() || event.ButtonDClick())
        {
            SetFocus();
        }
        if (m_viewOnly)
        {
            return;
        }

        // RFB button mask: bit 0 left, bit 1 middle, bit 2 right.
        uint8_t buttons = 0;
        buttons |= event.LeftIsDown() ? 1 : 0;
        buttons |= event.MiddleIsDown() ? 2 : 0;
        buttons |= event.RightIsDown() ? 4 : 0;

        // Keep receiving a drag that leaves the window, so the release still reaches the server.
        if (buttons != 0 && !HasCapture())
        {
            CaptureMouse();
        }
        else if (buttons == 0 && HasCapture())
        {
            ReleaseMouse();
        }

        wxPoint desktop = ToDesktop(event.GetPosition());
        if (buttons == m_buttons && desktop == m_lastPointer)
        {
            return;
        }
        m_buttons = buttons;
        SendPointer(event.GetPosition());
    }

    void VncCanvas::OnMouseWheel(wxMouseEvent& event)
    {
        if (m_viewOnly)
        {
            return;
        }
        // Accumulate so high-resolution wheels and trackpads send one click per notch worth of movement.
        bool horizontal = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL;
        int& accumulated = horizontal ? m_wheelHorizontal : m_wheelVertical;
        accumulated += event.GetWheelRotation();
        int delta = std::max(event.GetWheelDelta(), 1);
        while (std::abs(accumulated) >= delta)
        {
            bool positive = accumulated > 0;
            accumulated += positive ? -delta : delta;
            // Buttons 4/5 are wheel up/down, 6/7 wheel left/right. A positive horizontal rotation is rightwards.
            uint8_t bit = horizontal ? (positive ? 64 : 32) : (positive ? 8 : 16);
            m_buttons |= bit;
            SendPointer(event.GetPosition());
            m_buttons &= static_cast<uint8_t>(~bit);
            SendPointer(event.GetPosition());
        }
    }

    void VncCanvas::OnCaptureLost(wxMouseCaptureLostEvent&)
    {
        if (m_buttons != 0 && m_lastPointer.x >= 0)
        {
            m_client.SendPointerEvent(m_lastPointer.x, m_lastPointer.y, 0);
        }
        m_buttons = 0;
    }

    void VncCanvas::OnKeyDown(wxKeyEvent& event)
    {
        if (m_viewOnly || !m_keys.OnKeyDown(event))
        {
            event.Skip(); // let wxEVT_CHAR deliver the produced character
        }
    }

    void VncCanvas::OnKeyUp(wxKeyEvent& event)
    {
        if (!m_viewOnly)
        {
            m_keys.OnKeyUp(event);
        }
    }

    void VncCanvas::OnChar(wxKeyEvent& event)
    {
        if (!m_viewOnly)
        {
            m_keys.OnChar(event);
        }
    }

    void VncCanvas::OnKillFocus(wxFocusEvent& event)
    {
        ReleaseInput();
        event.Skip();
    }

    void VncCanvas::SetDesktopSize(int width, int height)
    {
        m_desktopSize = wxSize(width, height);
        if (width > 0 && height > 0)
        {
            m_backing = wxBitmap(width, height, 24);
            wxMemoryDC dc(m_backing);
            dc.SetBackground(*wxBLACK_BRUSH);
            dc.Clear();
        }
        else
        {
            m_backing = wxBitmap();
        }
        SetVirtualSize(m_desktopSize);
        CopyFromFramebuffer({0, 0, width, height});
        Refresh(false);
    }

    void VncCanvas::UpdateRects(const std::vector<RmVnc::Rfb::Rect>& rects)
    {
        for (const RmVnc::Rfb::Rect& rect : rects)
        {
            CopyFromFramebuffer(rect);
            wxPoint topLeft = CalcScrolledPosition(wxPoint(rect.x, rect.y));
            RefreshRect(wxRect(topLeft, wxSize(rect.width, rect.height)), false);
        }
    }

    void VncCanvas::CopyFromFramebuffer(const RmVnc::Rfb::Rect& requested)
    {
        if (!m_backing.IsOk())
        {
            return;
        }
        // The framebuffer may have been resized by the connection thread since this update was queued; clip to
        // both sizes so we never read or draw outside either.
        RmVnc::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
        wxImage image;
        RmVnc::Rfb::Rect rect = requested;
        {
            std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
            int right = std::min({rect.Right(), framebuffer.GetWidth(), m_backing.GetWidth()});
            int bottom = std::min({rect.Bottom(), framebuffer.GetHeight(), m_backing.GetHeight()});
            rect.x = std::max(rect.x, 0);
            rect.y = std::max(rect.y, 0);
            rect.width = right - rect.x;
            rect.height = bottom - rect.y;
            if (rect.IsEmpty())
            {
                return;
            }

            image.Create(rect.width, rect.height, false);
            unsigned char* out = image.GetData();
            for (int y = 0; y < rect.height; ++y)
            {
                const uint32_t* row = framebuffer.Row(rect.y + y) + rect.x;
                for (int x = 0; x < rect.width; ++x)
                {
                    uint32_t pixel = row[x];
                    *out++ = static_cast<unsigned char>(pixel >> 16);
                    *out++ = static_cast<unsigned char>(pixel >> 8);
                    *out++ = static_cast<unsigned char>(pixel);
                }
            }
        }

        wxMemoryDC dc(m_backing);
        dc.DrawBitmap(wxBitmap(image), rect.x, rect.y);
    }

    void VncCanvas::OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        DoPrepareDC(dc);

        wxSize client = GetClientSize();
        wxPoint origin = CalcUnscrolledPosition(wxPoint(0, 0));
        wxRect visible(origin, client);

        if (m_backing.IsOk())
        {
            wxMemoryDC source(m_backing);
            for (wxRegionIterator it(GetUpdateRegion()); it; ++it)
            {
                wxRect area = it.GetRect();
                area.SetPosition(CalcUnscrolledPosition(area.GetPosition()));
                wxRect inside = area.Intersect(wxRect(m_desktopSize));
                if (!inside.IsEmpty())
                {
                    dc.Blit(inside.GetPosition(), inside.GetSize(), &source, inside.GetPosition());
                }
            }
        }

        // Fill whatever part of the window lies outside the desktop.
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(kOutsideColour));
        if (visible.GetRight() >= m_desktopSize.x)
        {
            dc.DrawRectangle(m_desktopSize.x, visible.y, visible.GetRight() - m_desktopSize.x + 1, visible.height);
        }
        if (visible.GetBottom() >= m_desktopSize.y)
        {
            dc.DrawRectangle(visible.x, m_desktopSize.y, std::min(visible.width, m_desktopSize.x - visible.x),
                             visible.GetBottom() - m_desktopSize.y + 1);
        }
    }
} // namespace RmVncUI
