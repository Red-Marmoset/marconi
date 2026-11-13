// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux screen capture on X11: read the root window each grab, and report the 64x64 tiles that changed.
//
// This is the simple, reliable path (XGetImage), the Linux counterpart of the Windows GDI capture. It needs an X
// server (DISPLAY set); Wayland-only sessions have none, and the server then falls back to the test pattern.

#include "SystemScreen.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr int kTile = 64;

            class LinuxScreen : public ScreenSource
            {
            public:
                ~LinuxScreen() override
                {
                    if (m_display != nullptr)
                    {
                        XCloseDisplay(m_display);
                    }
                }

                bool Open(std::string& error)
                {
                    m_display = XOpenDisplay(nullptr);
                    if (m_display == nullptr)
                    {
                        error = "could not open the X display (is DISPLAY set? Wayland-only sessions aren't supported yet)";
                        return false;
                    }
                    m_root = DefaultRootWindow(m_display);
                    int eventBase = 0;
                    int errorBase = 0;
                    m_hasFixes = XFixesQueryExtension(m_display, &eventBase, &errorBase) != 0;
                    return true;
                }

                std::vector<Wire::Display> GetDisplays() override
                {
                    Measure();
                    Wire::Display display;
                    display.id = 1;
                    display.x = 0;
                    display.y = 0;
                    display.width = m_width;
                    display.height = m_height;
                    return {display};
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

                bool GetCursor(Cursor& out) override
                {
                    if (!m_hasFixes)
                    {
                        return false;
                    }
                    XFixesCursorImage* image = XFixesGetCursorImage(m_display);
                    if (image == nullptr)
                    {
                        return false;
                    }
                    out.width = image->width;
                    out.height = image->height;
                    out.hotspotX = image->xhot;
                    out.hotspotY = image->yhot;
                    out.argb.resize(static_cast<size_t>(image->width) * image->height);
                    for (size_t i = 0; i < out.argb.size(); ++i)
                    {
                        // XFixes gives premultiplied ARGB in an unsigned long; the wire wants alpha 0 or 255.
                        uint32_t pixel = static_cast<uint32_t>(image->pixels[i]);
                        uint32_t alpha = pixel >> 24;
                        out.argb[i] = (alpha >= 128 ? 0xFF000000u : 0u) | (pixel & 0x00FFFFFFu);
                    }
                    XFree(image);
                    return true;
                }

            private:
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
