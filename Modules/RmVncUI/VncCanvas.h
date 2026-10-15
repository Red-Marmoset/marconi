// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Scrollable view of the remote desktop, drawn from a backing bitmap kept in sync with the framebuffer

#pragma once

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

    private:
        void CopyFromFramebuffer(const RmVnc::Rfb::Rect& rect);
        void OnPaint(wxPaintEvent& event);

        RmVnc::Rfb::Client& m_client;
        wxBitmap m_backing;
        wxSize m_desktopSize;
    };
} // namespace RmVncUI
