// Copyright (c) 2026 Red Marmoset Studios Ltd.
// View of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer: scaled to fit the
// window, or at a fixed zoom level with scrollbars

#pragma once

#include "KeyMapper.h"

#include <MarconiCore/LocalKeys.h>
#include <MarconiCore/RemoteSession.h>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/region.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>

#include <functional>
#include <vector>

namespace MarconiUI
{
    enum class ScaleMode
    {
        Fit,   // scale the whole desktop into the window, keeping its aspect ratio
        Fixed, // a zoom level from Marconi::Zoom::kLevels (100 = one desktop pixel per screen pixel), scroll if needed
    };

    class VncCanvas : public wxScrolledCanvas
    {
    public:
        explicit VncCanvas(wxWindow* parent);

        // The connection the picture comes from and input goes to; null until the viewer has made one. UI thread.
        // The session must outlive the canvas or be replaced first.
        void SetSession(Marconi::RemoteSession* session) { m_session = session; }

        // UI thread only. The server's desktop changed size. Once a picture has been shown, it stays on screen
        // (scaled) until the server has sent the whole desktop at the new size, so partial or black pictures are
        // never shown.
        void SetDesktopSize(int width, int height);
        // UI thread only. Copies the changed areas from the framebuffer and repaints them.
        void UpdateRects(const std::vector<Marconi::Rfb::Rect>& rects);
        // False while an older picture is shown because the new size isn't complete yet.
        bool IsShowingCurrentPicture() const { return !m_pending; }
        // The picture currently shown, at desktop resolution (invalid before the first one).
        wxImage GetShownPicture() const;
        bool HasPicture() const { return m_hasPicture && m_backing.IsOk(); }

        wxSize GetDesktopSize() const { return m_desktopSize; }

        // Show only this part of the desktop (one of the server's displays), or everything when empty. Pointer
        // positions still go to the server in whole-desktop coordinates.
        void SetViewport(const wxRect& desktopRect);
        wxRect GetViewport() const { return m_viewport; }
        // The part of the desktop being shown: the viewport, or the whole desktop.
        wxRect GetPictureRect() const;

        // Fit, or a fixed zoom level. The level is kept while fitting, for when fitting is switched off again.
        void SetZoom(ScaleMode mode, int zoomPercent);
        void SetScaleMode(ScaleMode mode) { SetZoom(mode, m_zoomPercent); }
        ScaleMode GetScaleMode() const { return m_scaleMode; }
        int GetZoomPercent() const { return m_zoomPercent; }
        // Steps to the next level up or down from what's shown now, keeping the desktop point under `around` (a
        // window position) where it is.
        void ZoomStep(bool in, const wxPoint& around);
        // Current desktop-to-window scale factor, and the same as a percentage (what the status bar shows).
        double GetScale() const { return m_scale; }
        int GetShownPercent() const;

        // View-only ignores local input (and releases anything held when switched on).
        void SetViewOnly(bool viewOnly);
        // The connection was lost: the last picture stays, dimmed, and input is ignored until it comes back.
        void SetStale(bool stale);
        bool IsStale() const { return m_stale; }
        bool IsViewOnly() const { return m_viewOnly; }

        // Lifts every key and button the server thinks is held.
        void ReleaseInput();

        // UI thread only. Shows the server's cursor shape as our local cursor.
        void SetRemoteCursor(const Marconi::Rfb::CursorShape& cursor);
        // The server's cursor shape and where the pointer last was on the desktop (for screenshots that include it).
        bool HasRemoteCursor() const { return m_hasRemoteCursor && m_remoteCursor.width > 0; }
        const Marconi::Rfb::CursorShape& GetRemoteCursor() const { return m_remoteCursor; }
        wxPoint GetLastPointer() const { return m_lastPointer; }

        // Called instead of sending a key the viewer keeps for itself (F8 for the menu, F9 for the toolbar, the
        // zoom shortcuts).
        void SetLocalCommandHandler(std::function<void(Marconi::LocalCommand)> handler)
        {
            m_localCommandHandler = std::move(handler);
        }

        // Called with the paths of files dropped on the remote screen from Explorer or Finder.
        void SetFileDropHandler(std::function<void(const std::vector<std::string>&)> handler);

    private:
        void ApplyDesktopSize(int width, int height);
        bool FramebufferIsBlack();
        void UpdateLayout();
        void ApplyCursor();
        void CopyFromFramebuffer(const Marconi::Rfb::Rect& rect);
        void OnPaint(wxPaintEvent& event);
        void OnSize(wxSizeEvent& event);
        wxSize GetShownSize() const;
        void ZoomAround(int zoomPercent, const wxPoint& around);
        void UpdateEdgeScrolling(const wxPoint& windowPoint);
        void OnEdgeScroll();
        // A zoomed-in picture bigger than the window: it can be moved around.
        bool IsScrollable() const;
        void ScrollByPixels(const wxPoint& delta);
        // View only: dragging with the left button moves the picture instead of being sent.
        void PanByDrag(const wxMouseEvent& event);

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

        Marconi::RemoteSession* m_session = nullptr;
        wxBitmap m_backing;
        wxSize m_desktopSize; // size of the picture shown (the server's, unless a resize is pending)
        bool m_hasPicture = false;
        bool m_pending = false;  // the server has a new size whose picture isn't complete yet
        wxSize m_pendingSize;
        wxRegion m_coverage;     // what the server has sent since the resize
        wxRect m_viewport;       // empty: the whole desktop

        ScaleMode m_scaleMode = ScaleMode::Fit;
        int m_zoomPercent = 100;
        double m_scale = 1.0;
        wxPoint m_offset; // top-left of the scaled desktop in virtual (unscrolled) coordinates: centred when smaller
        wxTimer m_edgeScrollTimer; // full screen: the picture scrolls while the pointer rests at a window edge
        wxPoint m_edgeScrollPoint;
        int m_zoomWheel = 0; // Ctrl+wheel rotation not yet turned into a zoom step
        bool m_panning = false; // a view-only drag is moving the picture
        wxPoint m_panFrom;      // where the pointer was at the last pan step
        wxPoint m_panRemainder; // pixels not yet scrolled (scrolling moves in whole units)

        KeyMapper m_keys;
        bool m_viewOnly = false;
        bool m_stale = false;
        uint8_t m_buttons = 0;
        wxPoint m_lastPointer = wxPoint(-1, -1);
        int m_wheelVertical = 0;
        int m_wheelHorizontal = 0;
        Marconi::Rfb::CursorShape m_remoteCursor;
        bool m_hasRemoteCursor = false;
        double m_cursorScale = 0.0;
        std::function<void(Marconi::LocalCommand)> m_localCommandHandler;
        int m_keptKey = 0; // a kept key that is down: its release mustn't reach the server either
    };
} // namespace MarconiUI
