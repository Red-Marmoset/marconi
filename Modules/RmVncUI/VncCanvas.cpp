// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Scrollable view of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer

#include "VncCanvas.h"

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

#include <algorithm>

namespace RmVncUI
{
    namespace
    {
        const wxColour kOutsideColour(48, 48, 48);
    }

    VncCanvas::VncCanvas(wxWindow* parent, RmVnc::Rfb::Client& client)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          m_client(client)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(16, 16);
        Bind(wxEVT_PAINT, &VncCanvas::OnPaint, this);
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
