// Copyright (c) 2026 Red Marmoset Studios Ltd.
// View of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer: scaled to fit the
// window, or at a fixed zoom level with scrollbars

#include "VncCanvas.h"

#include "ClipboardFiles.h"
#include "ScaledBlit.h"

#include <MarconiCore/Zoom.h>

#include <wx/cursor.h>
#include <wx/dnd.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/toplevel.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace MarconiUI
{
    namespace
    {
        const wxColour kOutsideColour(48, 48, 48);
        constexpr int kEdgeScrollIntervalMs = 30;
    }

    VncCanvas::VncCanvas(wxWindow* parent, Marconi::Rfb::Client& client)
        : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
          m_client(client),
          m_keys([&client](uint32_t keysym, bool down) { client.SendKeyEvent(keysym, down); })
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(16, 16);
        Bind(wxEVT_PAINT, &VncCanvas::OnPaint, this);
        Bind(wxEVT_SIZE, &VncCanvas::OnSize, this);

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
        m_edgeScrollTimer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { OnEdgeScroll(); }, m_edgeScrollTimer.GetId());
        UpdateLayout();
    }

    void VncCanvas::SetDesktopSize(int width, int height)
    {
        if (m_hasPicture && width > 0 && height > 0)
        {
            // A Mac switching display modes sends partial pictures, or nothing, at the new size for a while; keep
            // the last complete picture until the new one covers the whole desktop.
            m_pending = true;
            m_pendingSize = wxSize(width, height);
            m_coverage.Clear();
            return;
        }
        ApplyDesktopSize(width, height);
    }

    bool VncCanvas::FramebufferIsBlack()
    {
        // A Mac's screen goes dark for a moment while it switches modes; that's not a picture worth showing.
        Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
        std::lock_guard<std::mutex> lock(framebuffer.GetMutex());
        return framebuffer.IsBlack();
    }

    wxImage VncCanvas::GetShownPicture() const
    {
        if (!m_backing.IsOk())
        {
            return wxImage();
        }
        wxImage picture = m_backing.ConvertToImage();
        wxRect shown = GetPictureRect();
        if (shown.GetSize() != m_desktopSize)
        {
            picture = picture.GetSubImage(shown);
        }
        return picture;
    }

    wxRect VncCanvas::GetPictureRect() const
    {
        wxRect whole(wxPoint(0, 0), m_desktopSize);
        if (m_viewport.IsEmpty())
        {
            return whole;
        }
        wxRect shown = m_viewport.Intersect(whole);
        return shown.IsEmpty() ? whole : shown;
    }

    void VncCanvas::SetViewport(const wxRect& desktopRect)
    {
        m_viewport = desktopRect;
        UpdateLayout();
        Refresh(false);
    }

    void VncCanvas::ApplyDesktopSize(int width, int height)
    {
        m_pending = false;
        m_coverage.Clear();
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
        UpdateLayout();
        CopyFromFramebuffer({0, 0, width, height});
        Refresh(false);
    }

    void VncCanvas::SetZoom(ScaleMode mode, int zoomPercent)
    {
        m_scaleMode = mode;
        m_zoomPercent = std::clamp(zoomPercent, Marconi::Zoom::kLevels[0],
                                   Marconi::Zoom::kLevels[Marconi::Zoom::kLevelCount - 1]);
        UpdateLayout();
        Refresh(false);
    }

    int VncCanvas::GetShownPercent() const
    {
        return static_cast<int>(std::lround(m_scale * 100.0));
    }

    void VncCanvas::ZoomStep(bool in, const wxPoint& around)
    {
        ZoomAround(Marconi::Zoom::Next(GetShownPercent(), in), around);
    }

    void VncCanvas::ZoomAround(int zoomPercent, const wxPoint& around)
    {
        // Keep the desktop point under `around` where it is on screen, as far as the scroll range allows.
        wxPoint desktopPoint = ToDesktop(around);
        SetZoom(ScaleMode::Fixed, zoomPercent);
        wxPoint virtualPoint(m_offset.x + static_cast<int>(std::lround(desktopPoint.x * m_scale)),
                             m_offset.y + static_cast<int>(std::lround(desktopPoint.y * m_scale)));
        int unitX = 1;
        int unitY = 1;
        GetScrollPixelsPerUnit(&unitX, &unitY);
        Scroll(std::max(0, (virtualPoint.x - around.x) / std::max(unitX, 1)),
               std::max(0, (virtualPoint.y - around.y) / std::max(unitY, 1)));
        Refresh(false);
    }

    wxSize VncCanvas::GetShownSize() const
    {
        wxRect picture = GetPictureRect();
        return wxSize(static_cast<int>(std::lround(picture.width * m_scale)),
                      static_cast<int>(std::lround(picture.height * m_scale)));
    }

    void VncCanvas::UpdateLayout()
    {
        wxSize client = GetClientSize();
        if (m_desktopSize.x <= 0 || m_desktopSize.y <= 0)
        {
            m_scale = 1.0;
            m_offset = wxPoint();
            ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
            SetVirtualSize(client);
        }
        else if (m_scaleMode == ScaleMode::Fit)
        {
            ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
            SetVirtualSize(client);
            Scroll(0, 0);
            client = GetClientSize();
            wxRect picture = GetPictureRect();
            m_scale = std::min(static_cast<double>(client.x) / picture.width,
                               static_cast<double>(client.y) / picture.height);
            m_scale = std::max(m_scale, 0.01);
            wxSize shown = GetShownSize();
            m_offset = wxPoint((client.x - shown.x) / 2, (client.y - shown.y) / 2);
        }
        else
        {
            // A fixed level: scrollbars when the picture is bigger than the window, centred when it's smaller.
            m_scale = m_zoomPercent / 100.0;
            wxSize shown = GetShownSize();
            ShowScrollbars(wxSHOW_SB_DEFAULT, wxSHOW_SB_DEFAULT);
            SetVirtualSize(shown);
            client = GetClientSize();
            m_offset = wxPoint(std::max(0, (client.x - shown.x) / 2), std::max(0, (client.y - shown.y) / 2));
        }
        ApplyCursor();
    }

    void VncCanvas::OnSize(wxSizeEvent& event)
    {
        UpdateLayout();
        Refresh(false);
        event.Skip();
    }

    wxPoint VncCanvas::ToDesktop(const wxPoint& windowPoint) const
    {
        wxPoint unscrolled = CalcUnscrolledPosition(windowPoint);
        wxRect picture = GetPictureRect();
        wxPoint point(picture.x + static_cast<int>(std::floor((unscrolled.x - m_offset.x) / m_scale)),
                      picture.y + static_cast<int>(std::floor((unscrolled.y - m_offset.y) / m_scale)));
        wxSize target = m_desktopSize;
        if (m_pending && m_desktopSize.x > 0 && m_desktopSize.y > 0)
        {
            // An older picture is on screen: send positions in the server's current size.
            target = m_pendingSize;
            point.x = static_cast<int>(static_cast<long long>(point.x) * target.x / m_desktopSize.x);
            point.y = static_cast<int>(static_cast<long long>(point.y) * target.y / m_desktopSize.y);
        }
        point.x = std::clamp(point.x, 0, std::max(target.x - 1, 0));
        point.y = std::clamp(point.y, 0, std::max(target.y - 1, 0));
        return point;
    }

    wxRect VncCanvas::ToWindow(const wxRect& desktopRect) const
    {
        // Round outwards so the repainted area always covers every partially affected screen pixel.
        wxRect picture = GetPictureRect();
        int left = m_offset.x + static_cast<int>(std::floor((desktopRect.x - picture.x) * m_scale)) - 1;
        int top = m_offset.y + static_cast<int>(std::floor((desktopRect.y - picture.y) * m_scale)) - 1;
        int right = m_offset.x + static_cast<int>(std::ceil((desktopRect.x + desktopRect.width - picture.x) * m_scale)) + 1;
        int bottom = m_offset.y + static_cast<int>(std::ceil((desktopRect.y + desktopRect.height - picture.y) * m_scale)) + 1;
        return wxRect(CalcScrolledPosition(wxPoint(left, top)), wxSize(right - left, bottom - top));
    }

    void VncCanvas::UpdateRects(const std::vector<Marconi::Rfb::Rect>& rects)
    {
        if (m_pending)
        {
            for (const Marconi::Rfb::Rect& rect : rects)
            {
                m_coverage.Union(wxRect(rect.x, rect.y, rect.width, rect.height));
            }
            // wxRegion::Contains(rect) reports partial overlap as inside on Windows, so check what's missing.
            wxRegion missing{wxRect(m_pendingSize)};
            missing.Subtract(m_coverage);
            if (missing.IsEmpty() && !FramebufferIsBlack())
            {
                ApplyDesktopSize(m_pendingSize.x, m_pendingSize.y); // copies the whole framebuffer
            }
            return;
        }
        m_hasPicture = m_hasPicture || !rects.empty();
        for (const Marconi::Rfb::Rect& rect : rects)
        {
            CopyFromFramebuffer(rect);
            RefreshRect(ToWindow(wxRect(rect.x, rect.y, rect.width, rect.height)), false);
        }
    }

    void VncCanvas::CopyFromFramebuffer(const Marconi::Rfb::Rect& requested)
    {
        if (!m_backing.IsOk())
        {
            return;
        }
        // The framebuffer may have been resized by the connection thread since this update was queued; clip to
        // both sizes so we never read or draw outside either.
        Marconi::Rfb::Framebuffer& framebuffer = m_client.GetFramebuffer();
        wxImage image;
        Marconi::Rfb::Rect rect = requested;
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
        DoPrepareDC(dc); // from here on the DC takes virtual (unscrolled) coordinates
        wxSize client = GetClientSize();
        wxRect visible(CalcUnscrolledPosition(wxPoint(0, 0)), client);
        wxRect shown(m_offset, GetShownSize());
        wxRect picture = GetPictureRect();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(kOutsideColour));

        if (m_backing.IsOk())
        {
            wxMemoryDC source(m_backing);
            bool unscaled = std::abs(m_scale - 1.0) < 1e-9;
            for (wxRegionIterator it(GetUpdateRegion()); it; ++it)
            {
                wxRect area = it.GetRect();
                area.SetPosition(CalcUnscrolledPosition(area.GetPosition()));
                area = area.Intersect(shown);
                if (area.IsEmpty())
                {
                    continue;
                }
                if (unscaled)
                {
                    dc.Blit(area.GetPosition(), area.GetSize(), &source, area.GetPosition() - m_offset + picture.GetPosition());
                    continue;
                }
                // Map each damaged area back to the desktop pixels covering it (plus a pixel of margin for the
                // filter) and stretch those; the clip region keeps neighbouring draws seamless.
                // x0..y1 are relative to the picture (the viewport); the backing bitmap holds the whole desktop.
                int x0 = std::max(0, static_cast<int>(std::floor((area.x - m_offset.x) / m_scale)) - 1);
                int y0 = std::max(0, static_cast<int>(std::floor((area.y - m_offset.y) / m_scale)) - 1);
                int x1 = std::min(picture.width, static_cast<int>(std::ceil((area.GetRight() + 1 - m_offset.x) / m_scale)) + 1);
                int y1 = std::min(picture.height, static_cast<int>(std::ceil((area.GetBottom() + 1 - m_offset.y) / m_scale)) + 1);
                wxRect sourceRect(picture.x + x0, picture.y + y0, x1 - x0, y1 - y0);
                wxRect destinationRect(m_offset.x + static_cast<int>(std::lround(x0 * m_scale)),
                                       m_offset.y + static_cast<int>(std::lround(y0 * m_scale)),
                                       static_cast<int>(std::lround(x1 * m_scale)) - static_cast<int>(std::lround(x0 * m_scale)),
                                       static_cast<int>(std::lround(y1 * m_scale)) - static_cast<int>(std::lround(y0 * m_scale)));
                ScaledBlit(dc, destinationRect, source, sourceRect);
            }
        }
        // Fill whatever part of the window lies outside the picture: bars above, below, left and right of it.
        auto fill = [&dc](int x, int y, int width, int height) {
            if (width > 0 && height > 0)
            {
                dc.DrawRectangle(x, y, width, height);
            }
        };
        fill(visible.x, visible.y, visible.width, shown.y - visible.y);
        fill(visible.x, shown.GetBottom() + 1, visible.width, visible.GetBottom() - shown.GetBottom());
        fill(visible.x, shown.y, shown.x - visible.x, shown.height);
        fill(shown.GetRight() + 1, shown.y, visible.GetRight() - shown.GetRight(), shown.height);

        if (m_stale)
        {
            // The connection is gone: dim the old picture so nobody mistakes it for a live one.
            wxGCDC overlay(dc);
            overlay.SetDeviceOrigin(0, 0);
            overlay.SetPen(*wxTRANSPARENT_PEN);
            overlay.SetBrush(wxBrush(wxColour(0, 0, 0, 128)));
            overlay.DrawRectangle(0, 0, client.x, client.y);
        }
    }

    void VncCanvas::SetStale(bool stale)
    {
        if (stale && !m_stale)
        {
            ReleaseInput();
            m_edgeScrollTimer.Stop();
        }
        m_stale = stale;
        Refresh(false);
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

    void VncCanvas::SetRemoteCursor(const Marconi::Rfb::CursorShape& cursor)
    {
        m_remoteCursor = cursor;
        m_hasRemoteCursor = true;
        m_cursorScale = 0.0; // force rebuild
        ApplyCursor();
    }

    namespace
    {
        // Windows ignores a cursor's alpha channel when every alpha value is zero, and then its empty mask makes the
        // whole thing opaque: a hidden pointer would show as a black square. One pixel at alpha 1 (invisible) keeps
        // the channel in use. Other systems honour the alpha either way.
        void KeepAlphaInUse(wxImage& image)
        {
            if (!image.HasAlpha())
            {
                image.InitAlpha();
            }
            const unsigned char* alpha = image.GetAlpha();
            size_t count = static_cast<size_t>(image.GetWidth()) * static_cast<size_t>(image.GetHeight());
            for (size_t i = 0; i < count; ++i)
            {
                if (alpha[i] != 0)
                {
                    return;
                }
            }
            image.SetRGB(0, 0, 0, 0, 0);
            image.SetAlpha(0, 0, 1);
        }

        wxCursor MakeCursor(wxImage image, int hotspotX, int hotspotY)
        {
            KeepAlphaInUse(image);
            image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotspotX);
            image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotspotY);
            return wxCursor(image);
        }
    } // namespace

    void VncCanvas::ApplyCursor()
    {
        if (!m_hasRemoteCursor || m_cursorScale == m_scale)
        {
            return;
        }
        m_cursorScale = m_scale;
        const Marconi::Rfb::CursorShape& cursor = m_remoteCursor;
        if (cursor.width <= 0 || cursor.height <= 0)
        {
            // The server hid its pointer (TightVNC does while you type). An invisible cursor: wxCURSOR_BLANK would
            // need wxWidgets' own resource file on Windows. Not 1x1: Windows scales tiny cursors up with the
            // pointer size setting, and the pixel KeepAlphaInUse leaves would show.
            wxImage blank(32, 32);
            blank.InitAlpha();
            memset(blank.GetAlpha(), 0, 32 * 32);
            SetCursor(MakeCursor(blank, 0, 0));
            return;
        }

        wxImage image(cursor.width, cursor.height, false);
        image.InitAlpha();
        unsigned char* rgb = image.GetData();
        unsigned char* alpha = image.GetAlpha();
        for (size_t i = 0; i < cursor.argb.size(); ++i)
        {
            uint32_t pixel = cursor.argb[i];
            rgb[i * 3 + 0] = static_cast<unsigned char>(pixel >> 16);
            rgb[i * 3 + 1] = static_cast<unsigned char>(pixel >> 8);
            rgb[i * 3 + 2] = static_cast<unsigned char>(pixel);
            alpha[i] = static_cast<unsigned char>(pixel >> 24);
        }
        int hotspotX = std::clamp(cursor.hotspotX, 0, cursor.width - 1);
        int hotspotY = std::clamp(cursor.hotspotY, 0, cursor.height - 1);
        // Match the cursor to the desktop's displayed size (but never smaller than half size, so it stays usable).
        double scale = std::max(m_scale, 0.5);
        if (std::abs(scale - 1.0) > 0.01)
        {
            int width = std::max(1, static_cast<int>(std::lround(cursor.width * scale)));
            int height = std::max(1, static_cast<int>(std::lround(cursor.height * scale)));
            image.Rescale(width, height, wxIMAGE_QUALITY_HIGH);
            hotspotX = std::min(static_cast<int>(hotspotX * scale), width - 1);
            hotspotY = std::min(static_cast<int>(hotspotY * scale), height - 1);
        }
        SetCursor(MakeCursor(image, hotspotX, hotspotY));
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
        if (event.Moving() || event.Dragging())
        {
            UpdateEdgeScrolling(event.GetPosition());
        }
        if (m_viewOnly || m_stale)
        {
            PanByDrag(event);
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

    void VncCanvas::UpdateEdgeScrolling(const wxPoint& windowPoint)
    {
        // In full screen there are no window edges to grab a scrollbar by, so a zoomed-in picture scrolls while the
        // pointer rests near an edge of the screen.
        wxTopLevelWindow* top = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
        wxSize client = GetClientSize();
        wxSize shown = GetShownSize();
        bool scrollable = top != nullptr && top->IsFullScreen() && m_scaleMode == ScaleMode::Fixed &&
                          (shown.x > client.x || shown.y > client.y);
        int edge = FromDIP(24);
        bool nearEdge = windowPoint.x < edge || windowPoint.y < edge || windowPoint.x >= client.x - edge ||
                        windowPoint.y >= client.y - edge;
        m_edgeScrollPoint = windowPoint;
        if (scrollable && nearEdge)
        {
            if (!m_edgeScrollTimer.IsRunning())
            {
                m_edgeScrollTimer.Start(kEdgeScrollIntervalMs);
            }
        }
        else
        {
            m_edgeScrollTimer.Stop();
        }
    }

    bool VncCanvas::IsScrollable() const
    {
        wxSize client = GetClientSize();
        wxSize shown = GetShownSize();
        return m_scaleMode == ScaleMode::Fixed && (shown.x > client.x || shown.y > client.y);
    }

    void VncCanvas::ScrollByPixels(const wxPoint& delta)
    {
        int unitX = 1;
        int unitY = 1;
        GetScrollPixelsPerUnit(&unitX, &unitY);
        unitX = std::max(unitX, 1);
        unitY = std::max(unitY, 1);
        m_panRemainder += delta;
        wxPoint units(m_panRemainder.x / unitX, m_panRemainder.y / unitY);
        m_panRemainder -= wxPoint(units.x * unitX, units.y * unitY);
        if (units == wxPoint())
        {
            return;
        }
        wxPoint before = GetViewStart();
        Scroll(std::max(0, before.x + units.x), std::max(0, before.y + units.y));
        if (GetViewStart() != before)
        {
            Refresh(false);
        }
    }

    void VncCanvas::PanByDrag(const wxMouseEvent& event)
    {
        if (event.LeftDown() && IsScrollable())
        {
            m_panning = true;
            m_panFrom = event.GetPosition();
            m_panRemainder = wxPoint();
            if (!HasCapture())
            {
                CaptureMouse();
            }
        }
        else if (m_panning && event.Dragging() && event.LeftIsDown())
        {
            ScrollByPixels(m_panFrom - event.GetPosition()); // the picture follows the pointer
            m_panFrom = event.GetPosition();
        }
        else if (m_panning && !event.LeftIsDown())
        {
            m_panning = false;
            if (HasCapture())
            {
                ReleaseMouse();
            }
        }
    }

    void VncCanvas::OnEdgeScroll()
    {
        wxSize client = GetClientSize();
        int edge = FromDIP(24);
        int step = FromDIP(16);
        int unitX = 1;
        int unitY = 1;
        GetScrollPixelsPerUnit(&unitX, &unitY);
        wxPoint before = GetViewStart();
        wxPoint target = before;
        if (m_edgeScrollPoint.x < edge)
        {
            target.x -= step / std::max(unitX, 1);
        }
        else if (m_edgeScrollPoint.x >= client.x - edge)
        {
            target.x += step / std::max(unitX, 1);
        }
        if (m_edgeScrollPoint.y < edge)
        {
            target.y -= step / std::max(unitY, 1);
        }
        else if (m_edgeScrollPoint.y >= client.y - edge)
        {
            target.y += step / std::max(unitY, 1);
        }
        Scroll(std::max(0, target.x), std::max(0, target.y));
        if (GetViewStart() == before)
        {
            m_edgeScrollTimer.Stop(); // the end of the picture
            return;
        }
        Refresh(false);
        if (!m_viewOnly)
        {
            SendPointer(m_edgeScrollPoint); // the desktop under the pointer moved
        }
    }

    void VncCanvas::OnMouseWheel(wxMouseEvent& event)
    {
        if (event.CmdDown())
        {
            // Ctrl (Cmd on a Mac) and the wheel zoom the view, and with Shift as well they scroll a zoomed-in
            // picture; neither reaches the remote computer.
            if (event.ShiftDown())
            {
                int pixels = event.GetWheelRotation() * FromDIP(48) / std::max(event.GetWheelDelta(), 1);
                bool horizontal = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL;
                ScrollByPixels(horizontal ? wxPoint(pixels, 0) : wxPoint(0, -pixels)); // wheel up shows what's above
                return;
            }
            if (event.AltDown() || event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL)
            {
                return;
            }
            m_zoomWheel += event.GetWheelRotation();
            int delta = std::max(event.GetWheelDelta(), 1);
            while (std::abs(m_zoomWheel) >= delta)
            {
                bool in = m_zoomWheel > 0;
                m_zoomWheel += in ? -delta : delta;
                ZoomStep(in, event.GetPosition());
            }
            return;
        }
        if (m_viewOnly || m_stale)
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
        m_panning = false;
        if (m_buttons != 0 && m_lastPointer.x >= 0)
        {
            m_client.SendPointerEvent(m_lastPointer.x, m_lastPointer.y, 0);
        }
        m_buttons = 0;
    }

    void VncCanvas::OnKeyDown(wxKeyEvent& event)
    {
        int code = event.GetKeyCode();
        if (m_localCommandHandler)
        {
            Marconi::LocalCommand command = Marconi::LocalCommand::None;
            if (code >= WXK_F1 && code <= WXK_F24)
            {
                command = Marconi::GetLocalCommand(code - WXK_F1 + 1, event.GetModifiers() != wxMOD_NONE);
            }
            else
            {
                char character = 0;
                if (code == WXK_NUMPAD_ADD || code == WXK_ADD)
                {
                    character = '+';
                }
                else if (code == WXK_NUMPAD_SUBTRACT || code == WXK_SUBTRACT)
                {
                    character = '-';
                }
                else if (code == WXK_NUMPAD0)
                {
                    character = '0';
                }
                else if (event.GetUnicodeKey() != WXK_NONE && event.GetUnicodeKey() < 128)
                {
                    character = static_cast<char>(event.GetUnicodeKey());
                }
                command = Marconi::GetLocalZoomCommand(character, event.CmdDown(), event.AltDown());
                if (command == Marconi::LocalCommand::None && (character == 'v' || character == 'V') &&
                    event.CmdDown() && !event.AltDown() && !event.ShiftDown() && ClipboardHasFiles())
                {
                    command = Marconi::LocalCommand::PasteFiles; // files copied locally: Paste sends them over
                }
            }
            if (command != Marconi::LocalCommand::None)
            {
                m_keptKey = code;
                m_localCommandHandler(command);
                return;
            }
        }
        if (m_viewOnly || m_stale || !m_keys.OnKeyDown(event))
        {
            event.Skip(); // let wxEVT_CHAR deliver the produced character
        }
    }

    void VncCanvas::OnKeyUp(wxKeyEvent& event)
    {
        if (m_keptKey != 0 && event.GetKeyCode() == m_keptKey)
        {
            m_keptKey = 0;
            return;
        }
        if (!m_viewOnly)
        {
            m_keys.OnKeyUp(event);
        }
    }

    void VncCanvas::OnChar(wxKeyEvent& event)
    {
        if (!m_viewOnly && !m_stale)
        {
            m_keys.OnChar(event);
        }
    }

    namespace
    {
        class CanvasFileDrop : public wxFileDropTarget
        {
        public:
            explicit CanvasFileDrop(std::function<void(const std::vector<std::string>&)> handler)
                : m_handler(std::move(handler))
            {
            }

            bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override
            {
                std::vector<std::string> paths;
                for (wxString file : files)
                {
                    file.Replace("\\", "/");
                    paths.push_back(file.utf8_string());
                }
                m_handler(paths);
                return true;
            }

        private:
            std::function<void(const std::vector<std::string>&)> m_handler;
        };
    } // namespace

    void VncCanvas::SetFileDropHandler(std::function<void(const std::vector<std::string>&)> handler)
    {
        SetDropTarget(new CanvasFileDrop(std::move(handler)));
    }

    void VncCanvas::OnKillFocus(wxFocusEvent& event)
    {
        ReleaseInput();
        event.Skip();
    }
} // namespace MarconiUI
