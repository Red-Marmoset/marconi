// Copyright (c) 2026 Red Marmoset Studios Ltd.
// View of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer: actual size with
// scrollbars, or scaled to fit the window

#include "VncCanvas.h"

#include "ScaledBlit.h"

#include <wx/cursor.h>
#include <wx/dnd.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace MarconiUI
{
    namespace
    {
        const wxColour kOutsideColour(48, 48, 48);
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

    void VncCanvas::SetScaleMode(ScaleMode mode)
    {
        m_scaleMode = mode;
        UpdateLayout();
        Refresh(false);
    }

    void VncCanvas::UpdateLayout()
    {
        wxSize client = GetClientSize();
        if (m_scaleMode == ScaleMode::Actual || m_desktopSize.x <= 0 || m_desktopSize.y <= 0)
        {
            m_scale = 1.0;
            m_offset = wxPoint();
            ShowScrollbars(wxSHOW_SB_DEFAULT, wxSHOW_SB_DEFAULT);
            SetVirtualSize(m_desktopSize);
        }
        else
        {
            ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
            SetVirtualSize(client);
            Scroll(0, 0);
            client = GetClientSize();
            m_scale = std::min(static_cast<double>(client.x) / m_desktopSize.x,
                               static_cast<double>(client.y) / m_desktopSize.y);
            m_scale = std::max(m_scale, 0.01);
            wxSize shown(static_cast<int>(std::lround(m_desktopSize.x * m_scale)),
                         static_cast<int>(std::lround(m_desktopSize.y * m_scale)));
            m_offset = wxPoint((client.x - shown.x) / 2, (client.y - shown.y) / 2);
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
        wxPoint point;
        if (m_scaleMode == ScaleMode::Actual)
        {
            point = CalcUnscrolledPosition(windowPoint);
        }
        else
        {
            point.x = static_cast<int>(std::floor((windowPoint.x - m_offset.x) / m_scale));
            point.y = static_cast<int>(std::floor((windowPoint.y - m_offset.y) / m_scale));
        }
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
        if (m_scaleMode == ScaleMode::Actual)
        {
            return wxRect(CalcScrolledPosition(desktopRect.GetPosition()), desktopRect.GetSize());
        }
        // Round outwards so the repainted area always covers every partially affected screen pixel.
        int left = m_offset.x + static_cast<int>(std::floor(desktopRect.x * m_scale)) - 1;
        int top = m_offset.y + static_cast<int>(std::floor(desktopRect.y * m_scale)) - 1;
        int right = m_offset.x + static_cast<int>(std::ceil((desktopRect.x + desktopRect.width) * m_scale)) + 1;
        int bottom = m_offset.y + static_cast<int>(std::ceil((desktopRect.y + desktopRect.height) * m_scale)) + 1;
        return wxRect(left, top, right - left, bottom - top);
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
        DoPrepareDC(dc);
        wxSize client = GetClientSize();
        wxRect visible(CalcUnscrolledPosition(wxPoint(0, 0)), client);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(kOutsideColour));

        if (m_scaleMode == ScaleMode::Actual)
        {
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
            if (visible.GetRight() >= m_desktopSize.x)
            {
                dc.DrawRectangle(m_desktopSize.x, visible.y, visible.GetRight() - m_desktopSize.x + 1, visible.height);
            }
            if (visible.GetBottom() >= m_desktopSize.y)
            {
                dc.DrawRectangle(visible.x, m_desktopSize.y, std::min(visible.width, m_desktopSize.x - visible.x),
                                 visible.GetBottom() - m_desktopSize.y + 1);
            }
            return;
        }

        // Fit mode: no scrolling. Map each damaged window area back to the desktop pixels covering it (plus a
        // pixel of margin for the filter) and stretch those; the clip region keeps neighbouring draws seamless.
        wxRect shown(m_offset, wxSize(static_cast<int>(std::lround(m_desktopSize.x * m_scale)),
                                      static_cast<int>(std::lround(m_desktopSize.y * m_scale))));
        if (m_backing.IsOk())
        {
            wxMemoryDC source(m_backing);
            for (wxRegionIterator it(GetUpdateRegion()); it; ++it)
            {
                wxRect area = it.GetRect().Intersect(shown);
                if (area.IsEmpty())
                {
                    continue;
                }
                int x0 = std::max(0, static_cast<int>(std::floor((area.x - m_offset.x) / m_scale)) - 1);
                int y0 = std::max(0, static_cast<int>(std::floor((area.y - m_offset.y) / m_scale)) - 1);
                int x1 = std::min(m_desktopSize.x, static_cast<int>(std::ceil((area.GetRight() + 1 - m_offset.x) / m_scale)) + 1);
                int y1 = std::min(m_desktopSize.y, static_cast<int>(std::ceil((area.GetBottom() + 1 - m_offset.y) / m_scale)) + 1);
                wxRect sourceRect(x0, y0, x1 - x0, y1 - y0);
                wxRect destinationRect(m_offset.x + static_cast<int>(std::lround(x0 * m_scale)),
                                       m_offset.y + static_cast<int>(std::lround(y0 * m_scale)),
                                       static_cast<int>(std::lround(x1 * m_scale)) - static_cast<int>(std::lround(x0 * m_scale)),
                                       static_cast<int>(std::lround(y1 * m_scale)) - static_cast<int>(std::lround(y0 * m_scale)));
                ScaledBlit(dc, destinationRect, source, sourceRect);
            }
        }
        // Letterbox bars.
        dc.DrawRectangle(0, 0, client.x, shown.y);
        dc.DrawRectangle(0, shown.GetBottom() + 1, client.x, client.y - shown.GetBottom() - 1);
        dc.DrawRectangle(0, shown.y, shown.x, shown.height);
        dc.DrawRectangle(shown.GetRight() + 1, shown.y, client.x - shown.GetRight() - 1, shown.height);
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
            SetCursor(wxCursor(wxCURSOR_BLANK));
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
        image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotspotX);
        image.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotspotY);
        SetCursor(wxCursor(image));
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
        int code = event.GetKeyCode();
        if (code >= WXK_F1 && code <= WXK_F24 && m_localCommandHandler)
        {
            Marconi::LocalCommand command = Marconi::GetLocalCommand(code - WXK_F1 + 1,
                                                                     event.GetModifiers() != wxMOD_NONE);
            if (command != Marconi::LocalCommand::None)
            {
                m_keptKey = code;
                m_localCommandHandler(command);
                return;
            }
        }
        if (m_viewOnly || !m_keys.OnKeyDown(event))
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
        if (!m_viewOnly)
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
