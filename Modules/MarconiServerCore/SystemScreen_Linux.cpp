// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux screen capture on X11: read the root window each grab, and report the 64x64 tiles that changed. Also the
// display layout (RandR monitors), changing an output's mode, and the pointer's image and position (XFixes).
//
// This is the simple, reliable path (XGetImage), the Linux counterpart of the Windows GDI capture. It needs an X
// server (DISPLAY set); Wayland-only sessions have none, and the server then falls back to the test pattern.

#include "SystemScreen.h"

#include "X11Support.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

// Xlib defines Status as a macro, which would break the protocol's own Status enums.
#undef Status

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr int kTile = 64;

            // Xlib's default reaction to a failed request is to exit the process. Around the RandR calls that can
            // fail on a bad mode, this records the failure instead.
            int g_xError = 0;
            int RecordXError(Display*, XErrorEvent* event)
            {
                g_xError = event->error_code != 0 ? event->error_code : 1;
                return 0;
            }

            class XErrorTrap
            {
            public:
                explicit XErrorTrap(Display* display) : m_display(display)
                {
                    XSync(m_display, False);
                    g_xError = 0;
                    m_previous = XSetErrorHandler(RecordXError);
                }
                ~XErrorTrap()
                {
                    XSync(m_display, False);
                    XSetErrorHandler(m_previous);
                }
                bool Failed() const
                {
                    XSync(m_display, False);
                    return g_xError != 0;
                }

            private:
                Display* m_display;
                int (*m_previous)(Display*, XErrorEvent*);
            };

            // What a CRTC (one output's scan-out) looked like before we changed it, to put it back on disconnect.
            struct SavedCrtc
            {
                RRCrtc crtc = 0;
                RRMode mode = 0;
                int x = 0;
                int y = 0;
                Rotation rotation = RR_Rotate_0;
                std::vector<RROutput> outputs;
            };

            class LinuxScreen : public ScreenSource
            {
            public:
                ~LinuxScreen() override
                {
                    RestoreDisplayModes();
                    if (m_display != nullptr)
                    {
                        XCloseDisplay(m_display);
                    }
                }

                bool Open(std::string& error)
                {
                    if (!X11OpenDisplay(m_display, error))
                    {
                        return false;
                    }
                    m_root = DefaultRootWindow(m_display);
                    const int defaultScreen = DefaultScreen(m_display);
                    m_basePxWidth = std::max(1, DisplayWidth(m_display, defaultScreen));
                    m_basePxHeight = std::max(1, DisplayHeight(m_display, defaultScreen));
                    m_baseMmWidth = std::max(1, DisplayWidthMM(m_display, defaultScreen));
                    m_baseMmHeight = std::max(1, DisplayHeightMM(m_display, defaultScreen));
                    int eventBase = 0;
                    int errorBase = 0;
                    m_hasFixes = XFixesQueryExtension(m_display, &eventBase, &errorBase) != 0;
                    if (m_hasFixes)
                    {
                        m_fixesEventBase = eventBase;
                        // Tell us when the pointer's image changes, so GetCursor only has to fetch it then.
                        XFixesSelectCursorInput(m_display, m_root, XFixesDisplayCursorNotifyMask);
                    }
                    int randrEventBase = 0;
                    int randrErrorBase = 0;
                    int major = 0;
                    int minor = 0;
                    m_hasRandr = XRRQueryExtension(m_display, &randrEventBase, &randrErrorBase) != 0 &&
                                 XRRQueryVersion(m_display, &major, &minor) != 0 &&
                                 (major > 1 || (major == 1 && minor >= 5));
                    return true;
                }

                // The monitors RandR reports (real outputs, and any virtual monitors set up with xrandr --setmonitor),
                // primary first. A monitor's id is its name atom: unique, and unchanged by a mode change. Without
                // RandR 1.5 the whole root window is the one display.
                std::vector<Wire::Display> GetDisplays() override
                {
                    Measure();
                    std::vector<Wire::Display> displays;
                    if (m_hasRandr)
                    {
                        int count = 0;
                        XRRMonitorInfo* monitors = XRRGetMonitors(m_display, m_root, True, &count);
                        if (monitors != nullptr)
                        {
                            for (int i = 0; i < count; ++i)
                            {
                                Wire::Display display;
                                display.id = static_cast<uint32_t>(monitors[i].name);
                                display.x = monitors[i].x;
                                display.y = monitors[i].y;
                                display.width = monitors[i].width;
                                display.height = monitors[i].height;
                                if (display.width <= 0 || display.height <= 0)
                                {
                                    continue;
                                }
                                if (monitors[i].primary != 0)
                                {
                                    displays.insert(displays.begin(), display);
                                }
                                else
                                {
                                    displays.push_back(display);
                                }
                            }
                            XRRFreeMonitors(monitors);
                        }
                    }
                    if (displays.empty())
                    {
                        Wire::Display display;
                        display.id = 1;
                        display.width = m_width;
                        display.height = m_height;
                        displays.push_back(display);
                    }
                    return displays;
                }

                bool CanSetDisplayMode() override { return m_hasRandr; }

                // Changes the monitor's first output to the listed mode nearest width x height. Only modes the output
                // itself lists are used; anything else is refused and nothing is touched.
                Wire::DisplayModeResult::Status SetDisplayMode(uint32_t displayId, int32_t width,
                                                               int32_t height) override
                {
                    using Status = Wire::DisplayModeResult::Status;
                    if (!m_hasRandr)
                    {
                        return Status::NotAllowed;
                    }
                    if (width <= 0 || height <= 0)
                    {
                        return Status::NotListed;
                    }
                    int count = 0;
                    XRRMonitorInfo* monitors = XRRGetMonitors(m_display, m_root, True, &count);
                    if (monitors == nullptr)
                    {
                        return Status::Failed;
                    }
                    RROutput output = 0;
                    bool found = false;
                    for (int i = 0; i < count; ++i)
                    {
                        if (static_cast<uint32_t>(monitors[i].name) == displayId)
                        {
                            found = true;
                            output = monitors[i].noutput > 0 ? monitors[i].outputs[0] : 0;
                        }
                    }
                    XRRFreeMonitors(monitors);
                    if (!found)
                    {
                        return Status::NoSuchDisplay;
                    }
                    if (output == 0)
                    {
                        return Status::Failed; // a virtual monitor: no output behind it whose mode could change
                    }
                    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(m_display, m_root);
                    if (resources == nullptr)
                    {
                        return Status::Failed;
                    }
                    Status status = ApplyMode(resources, output, width, height);
                    XRRFreeScreenResources(resources);
                    return status;
                }

                // Puts every output we changed back, then the desktop's size.
                void RestoreDisplayModes() override
                {
                    if (m_display == nullptr || m_saved.empty())
                    {
                        return;
                    }
                    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(m_display, m_root);
                    if (resources != nullptr)
                    {
                        XErrorTrap trap(m_display);
                        Measure(); // Xlib's own DisplayWidth is cached from connecting and doesn't follow changes
                        // Grow first so the restored mode fits, set the outputs back, then the original size.
                        SetScreenSizeClamped(std::max(m_width, m_originalWidth), std::max(m_height, m_originalHeight),
                                             MmFor(std::max(m_width, m_originalWidth), true),
                                             MmFor(std::max(m_height, m_originalHeight), false));
                        for (SavedCrtc& saved : m_saved)
                        {
                            XRRSetCrtcConfig(m_display, resources, saved.crtc, CurrentTime, saved.x, saved.y,
                                             saved.mode, saved.rotation, saved.outputs.data(),
                                             static_cast<int>(saved.outputs.size()));
                        }
                        SetScreenSizeClamped(m_originalWidth, m_originalHeight, MmFor(m_originalWidth, true),
                                             MmFor(m_originalHeight, false));
                        XRRFreeScreenResources(resources);
                    }
                    m_saved.clear();
                }

                bool Grab(Capture& out) override
                {
                    Measure();
                    if (m_width <= 0 || m_height <= 0)
                    {
                        return false;
                    }
                    XImage* image = XGetImage(m_display, m_root, 0, 0, static_cast<unsigned>(m_width),
                                              static_cast<unsigned>(m_height), AllPlanes, ZPixmap);
                    if (image == nullptr)
                    {
                        return false;
                    }
                    bool ok = Convert(*image, out);
                    XDestroyImage(image);
                    if (!ok)
                    {
                        return false;
                    }
                    FindDirty(out);
                    return true;
                }

                // True only when the pointer's image is new: the first call, and after the server says it changed.
                bool GetCursor(Cursor& out) override
                {
                    if (!m_hasFixes)
                    {
                        return false;
                    }
                    bool changed = !m_cursorSent;
                    while (XPending(m_display) > 0)
                    {
                        XEvent event;
                        XNextEvent(m_display, &event);
                        if (event.type == m_fixesEventBase + XFixesCursorNotify)
                        {
                            changed = true;
                        }
                    }
                    if (!changed)
                    {
                        return false;
                    }
                    XFixesCursorImage* image = XFixesGetCursorImage(m_display);
                    if (image == nullptr)
                    {
                        return false;
                    }
                    m_cursorSent = true;
                    out.width = image->width;
                    out.height = image->height;
                    out.hotspotX = image->xhot;
                    out.hotspotY = image->yhot;
                    out.argb.resize(static_cast<size_t>(image->width) * image->height);
                    bool anyVisible = false;
                    for (size_t i = 0; i < out.argb.size(); ++i)
                    {
                        // XFixes gives premultiplied ARGB in an unsigned long; the wire wants alpha 0 or 255 with
                        // the colour at full strength, so the colour is un-premultiplied.
                        uint32_t pixel = static_cast<uint32_t>(image->pixels[i]);
                        uint32_t alpha = pixel >> 24;
                        if (alpha < 128)
                        {
                            out.argb[i] = 0;
                            continue;
                        }
                        anyVisible = true;
                        uint32_t red = std::min<uint32_t>(255, ((pixel >> 16) & 0xFF) * 255 / alpha);
                        uint32_t green = std::min<uint32_t>(255, ((pixel >> 8) & 0xFF) * 255 / alpha);
                        uint32_t blue = std::min<uint32_t>(255, (pixel & 0xFF) * 255 / alpha);
                        out.argb[i] = 0xFF000000u | (red << 16) | (green << 8) | blue;
                    }
                    if (!anyVisible)
                    {
                        out.argb.clear(); // a fully clear image is how programs hide the pointer
                        out.width = 0;
                        out.height = 0;
                    }
                    XFree(image);
                    return true;
                }

                bool GetCursorPosition(int32_t& x, int32_t& y) override
                {
                    Window rootReturn;
                    Window childReturn;
                    int rootX = 0;
                    int rootY = 0;
                    int windowX = 0;
                    int windowY = 0;
                    unsigned int mask = 0;
                    if (XQueryPointer(m_display, m_root, &rootReturn, &childReturn, &rootX, &rootY, &windowX, &windowY,
                                      &mask) == 0)
                    {
                        return false;
                    }
                    x = rootX;
                    y = rootY;
                    return true;
                }

            private:
                // Sets one output's mode (see SetDisplayMode) and resizes the desktop to hold every active output.
                Wire::DisplayModeResult::Status ApplyMode(XRRScreenResources* resources, RROutput output, int width,
                                                          int height)
                {
                    using Status = Wire::DisplayModeResult::Status;
                    XRROutputInfo* outputInfo = XRRGetOutputInfo(m_display, resources, output);
                    if (outputInfo == nullptr)
                    {
                        return Status::Failed;
                    }
                    RRCrtc crtc = outputInfo->crtc;
                    // The sizes this output lists, and the one nearest what was asked for.
                    RRMode best = 0;
                    int bestWidth = 0;
                    int bestHeight = 0;
                    long bestDistance = -1;
                    for (int i = 0; i < outputInfo->nmode; ++i)
                    {
                        for (int m = 0; m < resources->nmode; ++m)
                        {
                            const XRRModeInfo& info = resources->modes[m];
                            if (info.id != outputInfo->modes[i] || (info.modeFlags & RR_Interlace) != 0)
                            {
                                continue;
                            }
                            long distance = std::labs(static_cast<long>(info.width) - width) +
                                            std::labs(static_cast<long>(info.height) - height);
                            if (bestDistance < 0 || distance < bestDistance)
                            {
                                best = info.id;
                                bestWidth = static_cast<int>(info.width);
                                bestHeight = static_cast<int>(info.height);
                                bestDistance = distance;
                            }
                        }
                    }
                    XRRFreeOutputInfo(outputInfo);
                    if (best == 0)
                    {
                        return Status::NotListed;
                    }
                    if (crtc == 0)
                    {
                        return Status::Failed; // the output is switched off
                    }

                    XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(m_display, resources, crtc);
                    if (crtcInfo == nullptr)
                    {
                        return Status::Failed;
                    }
                    if (crtcInfo->mode == best)
                    {
                        XRRFreeCrtcInfo(crtcInfo);
                        return Status::Ok; // already that size
                    }
                    if (crtcInfo->rotation != RR_Rotate_0)
                    {
                        XRRFreeCrtcInfo(crtcInfo);
                        return Status::Failed; // rotated screens aren't resized (their width and height swap)
                    }

                    // What every active CRTC covers once this one has the new mode: that is the desktop's new size.
                    int newScreenWidth = 0;
                    int newScreenHeight = 0;
                    for (int i = 0; i < resources->ncrtc; ++i)
                    {
                        if (resources->crtcs[i] == crtc)
                        {
                            newScreenWidth = std::max(newScreenWidth, crtcInfo->x + bestWidth);
                            newScreenHeight = std::max(newScreenHeight, crtcInfo->y + bestHeight);
                            continue;
                        }
                        XRRCrtcInfo* other = XRRGetCrtcInfo(m_display, resources, resources->crtcs[i]);
                        if (other == nullptr)
                        {
                            continue;
                        }
                        if (other->mode != None)
                        {
                            newScreenWidth = std::max(newScreenWidth, other->x + static_cast<int>(other->width));
                            newScreenHeight = std::max(newScreenHeight, other->y + static_cast<int>(other->height));
                        }
                        XRRFreeCrtcInfo(other);
                    }
                    int minWidth = 0;
                    int minHeight = 0;
                    int maxWidth = 0;
                    int maxHeight = 0;
                    XRRGetScreenSizeRange(m_display, m_root, &minWidth, &minHeight, &maxWidth, &maxHeight);
                    if (newScreenWidth < minWidth || newScreenHeight < minHeight || newScreenWidth > maxWidth ||
                        newScreenHeight > maxHeight)
                    {
                        XRRFreeCrtcInfo(crtcInfo);
                        return Status::NotListed; // would need a desktop size the server doesn't allow
                    }

                    Measure(); // the desktop's real size now (Xlib's DisplayWidth is cached from connecting)
                    int oldWidth = m_width;
                    int oldHeight = m_height;
                    if (m_saved.empty())
                    {
                        m_originalWidth = oldWidth;
                        m_originalHeight = oldHeight;
                    }
                    bool alreadySaved = false;
                    for (const SavedCrtc& saved : m_saved)
                    {
                        alreadySaved = alreadySaved || saved.crtc == crtc;
                    }
                    if (!alreadySaved)
                    {
                        SavedCrtc saved;
                        saved.crtc = crtc;
                        saved.mode = crtcInfo->mode;
                        saved.x = crtcInfo->x;
                        saved.y = crtcInfo->y;
                        saved.rotation = crtcInfo->rotation;
                        saved.outputs.assign(crtcInfo->outputs, crtcInfo->outputs + crtcInfo->noutput);
                        m_saved.push_back(saved);
                    }

                    XErrorTrap trap(m_display);
                    // Grow the desktop first if the new mode needs room, switch the output, then trim the desktop to
                    // fit. The real physical size is unknown, so the millimetres keep the same dots per inch.
                    int mmWidth = MmFor(newScreenWidth, true);
                    int mmHeight = MmFor(newScreenHeight, false);
                    if (newScreenWidth > oldWidth || newScreenHeight > oldHeight)
                    {
                        SetScreenSizeClamped(std::max(newScreenWidth, oldWidth), std::max(newScreenHeight, oldHeight),
                                             mmWidth, mmHeight);
                    }
                    Status result = Status::Ok;
                    int setStatus = XRRSetCrtcConfig(m_display, resources, crtc, CurrentTime, crtcInfo->x, crtcInfo->y,
                                                     best, crtcInfo->rotation, crtcInfo->outputs, crtcInfo->noutput);
                    if (setStatus != RRSetConfigSuccess || trap.Failed())
                    {
                        result = Status::Failed;
                    }
                    else
                    {
                        SetScreenSizeClamped(newScreenWidth, newScreenHeight, mmWidth, mmHeight);
                    }
                    XRRFreeCrtcInfo(crtcInfo);
                    return result;
                }

                int MmFor(int pixels, bool horizontal) const
                {
                    return horizontal ? m_baseMmWidth * pixels / m_basePxWidth : m_baseMmHeight * pixels / m_basePxHeight;
                }

                void SetScreenSizeClamped(int width, int height, int mmWidth, int mmHeight)
                {
                    int minWidth = 0;
                    int minHeight = 0;
                    int maxWidth = 0;
                    int maxHeight = 0;
                    XRRGetScreenSizeRange(m_display, m_root, &minWidth, &minHeight, &maxWidth, &maxHeight);
                    width = std::min(std::max(width, minWidth), maxWidth);
                    height = std::min(std::max(height, minHeight), maxHeight);
                    XRRSetScreenSize(m_display, m_root, width, height, std::max(mmWidth, 1), std::max(mmHeight, 1));
                }

                void Measure()
                {
                    XWindowAttributes attributes;
                    if (XGetWindowAttributes(m_display, m_root, &attributes) != 0)
                    {
                        m_width = attributes.width;
                        m_height = attributes.height;
                    }
                }

                // Turns the X image into 0x00RRGGBB, using the visual's own masks so it works whatever the byte order.
                bool Convert(const XImage& image, Capture& out)
                {
                    if (image.bits_per_pixel != 32 && image.bits_per_pixel != 24)
                    {
                        return false;
                    }
                    out.width = image.width;
                    out.height = image.height;
                    out.pixels.resize(static_cast<size_t>(image.width) * image.height);
                    const bool standard = image.bits_per_pixel == 32 && image.red_mask == 0xFF0000 &&
                                          image.green_mask == 0x00FF00 && image.blue_mask == 0x0000FF &&
                                          image.byte_order == LSBFirst;
                    for (int y = 0; y < image.height; ++y)
                    {
                        const unsigned char* row = reinterpret_cast<const unsigned char*>(image.data) +
                                                   static_cast<size_t>(y) * image.bytes_per_line;
                        uint32_t* destination = out.pixels.data() + static_cast<size_t>(y) * image.width;
                        if (standard)
                        {
                            std::memcpy(destination, row, static_cast<size_t>(image.width) * 4);
                            for (int x = 0; x < image.width; ++x)
                            {
                                destination[x] &= 0x00FFFFFFu;
                            }
                            continue;
                        }
                        for (int x = 0; x < image.width; ++x)
                        {
                            destination[x] = static_cast<uint32_t>(XGetPixel(const_cast<XImage*>(&image), x, y)) &
                                             0x00FFFFFFu;
                        }
                    }
                    return true;
                }

                // Compares with the last picture tile by tile; neighbouring changed tiles in a row become one rectangle.
                void FindDirty(Capture& out)
                {
                    out.dirty.clear();
                    const bool first = m_previous.size() != out.pixels.size() || m_previousWidth != m_width;
                    if (first)
                    {
                        out.dirty.push_back({0, 0, m_width, m_height});
                    }
                    else
                    {
                        for (int ty = 0; ty < m_height; ty += kTile)
                        {
                            int tileHeight = std::min(kTile, m_height - ty);
                            int runStart = -1;
                            for (int tx = 0; tx <= m_width; tx += kTile)
                            {
                                bool changed = false;
                                if (tx < m_width)
                                {
                                    int tileWidth = std::min(kTile, m_width - tx);
                                    for (int y = ty; y < ty + tileHeight && !changed; ++y)
                                    {
                                        size_t offset = static_cast<size_t>(y) * m_width + tx;
                                        changed = std::memcmp(out.pixels.data() + offset, m_previous.data() + offset,
                                                              static_cast<size_t>(tileWidth) * 4) != 0;
                                    }
                                }
                                if (changed && runStart < 0)
                                {
                                    runStart = tx;
                                }
                                else if (!changed && runStart >= 0)
                                {
                                    out.dirty.push_back({runStart, ty, std::min(tx, m_width) - runStart, tileHeight});
                                    runStart = -1;
                                }
                            }
                        }
                    }
                    m_previous = out.pixels;
                    m_previousWidth = m_width;
                }

                Display* m_display = nullptr;
                Window m_root = 0;
                bool m_hasFixes = false;
                int m_fixesEventBase = 0;
                bool m_cursorSent = false; // the first GetCursor always reports the shape
                bool m_hasRandr = false;   // RandR 1.5: monitors and mode changes
                std::vector<SavedCrtc> m_saved; // outputs we changed, in the order we changed them
                int m_originalWidth = 0;        // the desktop's size before the first change
                int m_originalHeight = 0;
                // The desktop's size in pixels and millimetres as Xlib reported it on connecting (a matching pair, even
                // though Xlib never updates them): the dots per inch a changed size keeps.
                int m_basePxWidth = 1;
                int m_basePxHeight = 1;
                int m_baseMmWidth = 1;
                int m_baseMmHeight = 1;
                int m_width = 0;
                int m_height = 0;
                int m_previousWidth = 0;
                std::vector<uint32_t> m_previous;
            };
        } // namespace

        std::unique_ptr<ScreenSource> CreateSystemScreen(std::string& error)
        {
            auto screen = std::make_unique<LinuxScreen>();
            if (!screen->Open(error))
            {
                return nullptr;
            }
            return screen;
        }
    } // namespace Server
} // namespace Marconi
